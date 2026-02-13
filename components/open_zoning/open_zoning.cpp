#include "open_zoning.h"

namespace esphome {
namespace open_zoning {

// ============================================================================
// Zone method implementations
// ============================================================================

bool Zone::calc_state() {
  state_new = ZoneState::OFF;
  bool error_triggered = false;

  // Error detection: Y1 or Y2 active without G (fan)
  if ((y1->state || y2->state) && !g->state) {
    error_count++;
    if (error_count == 1) {
      ESP_LOGW(TAG, "Zone %d error detected (count: 1/2) - Y1:%d Y2:%d G:%d",
               index + 1, y1->state, y2->state, g->state);
    }
    if (error_count >= 2) {
      ESP_LOGE(TAG, "Zone %d ERROR CONFIRMED (count: 2/2) - Y1:%d Y2:%d G:%d",
               index + 1, y1->state, y2->state, g->state);
      state_new = ZoneState::ERROR;
      error_triggered = true;
      return error_triggered;
    }
  } else {
    if (error_count > 0) {
      ESP_LOGI(TAG, "Zone %d error cleared (was at count: %d)", index + 1, error_count);
      error_count = 0;
    }
  }

  // State determination (highest priority first)
  if (y2->state && g->state && ob->state) {
    state_new = ZoneState::HEATING_STAGE2;
  } else if (y1->state && g->state && ob->state) {
    state_new = ZoneState::HEATING_STAGE1;
  } else if (y2->state && g->state && !ob->state) {
    state_new = ZoneState::COOLING_STAGE2;
  } else if (y1->state && g->state && !ob->state) {
    state_new = ZoneState::COOLING_STAGE1;
  } else if (g->state) {
    state_new = ZoneState::FAN_ONLY;
  }

  return error_triggered;
}

void Zone::apply_short_cycle_protection(unsigned long current_time, unsigned long min_cycle_time_ms) {
  // Track when a zone first becomes active (heating/cooling)
  if (state == ZoneState::OFF && is_active()) {
    active_start_ms = current_time;
    ESP_LOGI(TAG, "Zone %d started active cycle at %lu ms", index + 1, active_start_ms);
  }

  // Error clears protection immediately
  if (state_new == ZoneState::ERROR) {
    active_start_ms = 0;
    if (short_cycle_protection) {
      short_cycle_protection = false;
    }
    return;
  }

  if (was_active() && state_new == ZoneState::OFF) {
    // Zone transitioning from active to OFF — check minimum cycle time
    unsigned long elapsed = current_time - active_start_ms;
    if (elapsed < min_cycle_time_ms) {
      // Hold in previous state
      state_new = state;
      if (!short_cycle_protection) {
        short_cycle_protection = true;
      }
      ESP_LOGW(TAG, "Zone %d short cycle protection active - elapsed: %lu ms / required: %lu ms",
               index + 1, elapsed, min_cycle_time_ms);
    } else {
      if (short_cycle_protection) {
        short_cycle_protection = false;
      }
      active_start_ms = 0;
    }
  } else if (state_new != ZoneState::OFF) {
    // Zone still active — update protection flag based on elapsed time
    if (active_start_ms > 0) {
      unsigned long elapsed = current_time - active_start_ms;
      bool new_protection = (elapsed < min_cycle_time_ms);
      if (short_cycle_protection != new_protection) {
        short_cycle_protection = new_protection;
      }
    }
  } else {
    // Zone is OFF and was OFF (or non-active) — clear protection
    if (short_cycle_protection) {
      short_cycle_protection = false;
    }
  }
}

// ============================================================================
// OpenZoningController method implementations
// ============================================================================

void OpenZoningController::set_zone_sensors(uint8_t index,
                                            binary_sensor::BinarySensor *y1,
                                            binary_sensor::BinarySensor *y2,
                                            binary_sensor::BinarySensor *g,
                                            binary_sensor::BinarySensor *ob) {
  if (index >= MAX_ZONES) {
    ESP_LOGE(TAG, "Zone index %d exceeds MAX_ZONES (%d)", index, MAX_ZONES);
    return;
  }
  zones_[index].y1 = y1;
  zones_[index].y2 = y2;
  zones_[index].g = g;
  zones_[index].ob = ob;
}

void OpenZoningController::set_zone_dampers(uint8_t index,
                                            switch_::Switch *damper_open,
                                            switch_::Switch *damper_close) {
  if (index >= MAX_ZONES) {
    ESP_LOGE(TAG, "Zone index %d exceeds MAX_ZONES (%d)", index, MAX_ZONES);
    return;
  }
  zones_[index].damper_open_sw = damper_open;
  zones_[index].damper_close_sw = damper_close;
}

void OpenZoningController::setup() {
  ESP_LOGI(TAG, "OpenZoning initialized — %d zones configured", num_zones_);

  // Initialize all configured zones with their index
  for (uint8_t i = 0; i < num_zones_; i++) {
    zones_[i].index = i;
    zones_[i].state = ZoneState::OFF;
    zones_[i].state_new = ZoneState::OFF;
    zones_[i].damper_state = 255;  // Unknown — forces first update() to drive correct position
    zones_[i].error_count = 0;
    zones_[i].purge_end_ms = 0;
    zones_[i].active_start_ms = 0;
    zones_[i].short_cycle_protection = false;
    zones_[i].enabled = true;
  }

  // NOTE: Dampers are NOT driven in setup() — the first update() cycle
  // will determine the correct position based on actual zone demands.
  // This avoids I2C race conditions with MCP23017 during boot.

  // Initialize mode to Arrêt
  current_mode_ = 0;
  last_active_mode_ = 0;
  stage1_start_ms_ = 0;
}

void OpenZoningController::update() {
  if (num_zones_ == 0) {
    ESP_LOGW(TAG, "No zones configured — skipping update");
    return;
  }

  // Execute PASS 1–3
  pass1_calc_zone_states_();
  pass1_5_short_cycle_protection_();
  pass2_purge_management_();
  pass3_priority_analysis_();

  // Execute PASS 4–5
  pass4_damper_control_();
  pass5_output_control_();

  // Commit new states and log changes
  for (uint8_t i = 0; i < num_zones_; i++) {
    if (zones_[i].state != zones_[i].state_new) {
      ESP_LOGI(TAG, "Zone %d: %s -> %s",
               i + 1,
               state_to_string(zones_[i].state),
               state_to_string(zones_[i].state_new));
    }
    zones_[i].state = zones_[i].state_new;
  }

  // Log summary at debug level
  ESP_LOGD(TAG, "Update cycle complete — max_priority=%d error_flag=%s",
           global_max_priority_, zone_error_flag_ ? "YES" : "no");
}

void OpenZoningController::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenZoning Controller:");
  ESP_LOGCONFIG(TAG, "  Update interval: %.1fs", this->get_update_interval() / 1000.0f);
  ESP_LOGCONFIG(TAG, "  Zones configured: %d", num_zones_);
  ESP_LOGCONFIG(TAG, "  Min cycle time: %u ms", min_cycle_time_ms_);
  ESP_LOGCONFIG(TAG, "  Purge duration: %u ms", purge_duration_ms_);
  ESP_LOGCONFIG(TAG, "  Stage 2 escalation: %u ms", stage2_escalation_ms_);
  ESP_LOGCONFIG(TAG, "  Auto mode: %s", auto_mode_ ? "YES" : "NO");
  for (uint8_t i = 0; i < num_zones_; i++) {
    ESP_LOGCONFIG(TAG, "  Zone %d:", i + 1);
    ESP_LOGCONFIG(TAG, "    Y1: %s", zones_[i].y1 ? zones_[i].y1->get_name().c_str() : "NOT SET");
    ESP_LOGCONFIG(TAG, "    Y2: %s", zones_[i].y2 ? zones_[i].y2->get_name().c_str() : "NOT SET");
    ESP_LOGCONFIG(TAG, "    G:  %s", zones_[i].g  ? zones_[i].g->get_name().c_str()  : "NOT SET");
    ESP_LOGCONFIG(TAG, "    OB: %s", zones_[i].ob ? zones_[i].ob->get_name().c_str() : "NOT SET");
    ESP_LOGCONFIG(TAG, "    Damper Open:  %s", zones_[i].damper_open_sw  ? zones_[i].damper_open_sw->get_name().c_str()  : "NOT SET");
    ESP_LOGCONFIG(TAG, "    Damper Close: %s", zones_[i].damper_close_sw ? zones_[i].damper_close_sw->get_name().c_str() : "NOT SET");
  }
  ESP_LOGCONFIG(TAG, "  Outputs:");
  ESP_LOGCONFIG(TAG, "    Y1:  %s", out_y1_  ? out_y1_->get_name().c_str()  : "NOT SET");
  ESP_LOGCONFIG(TAG, "    Y2:  %s", out_y2_  ? out_y2_->get_name().c_str()  : "NOT SET");
  ESP_LOGCONFIG(TAG, "    G:   %s", out_g_   ? out_g_->get_name().c_str()   : "NOT SET");
  ESP_LOGCONFIG(TAG, "    OB:  %s", out_ob_  ? out_ob_->get_name().c_str()  : "NOT SET");
  ESP_LOGCONFIG(TAG, "    W1e: %s", out_w1e_ ? out_w1e_->get_name().c_str() : "NOT SET");
  ESP_LOGCONFIG(TAG, "    W2:  %s", out_w2_  ? out_w2_->get_name().c_str()  : "NOT SET");
  ESP_LOGCONFIG(TAG, "    W3:  %s", out_w3_  ? out_w3_->get_name().c_str()  : "NOT SET");
  ESP_LOGCONFIG(TAG, "  LEDs:");
  ESP_LOGCONFIG(TAG, "    Heat:  %s", led_heat_  ? led_heat_->get_name().c_str()  : "NOT SET");
  ESP_LOGCONFIG(TAG, "    Cool:  %s", led_cool_  ? led_cool_->get_name().c_str()  : "NOT SET");
  ESP_LOGCONFIG(TAG, "    Fan:   %s", led_fan_   ? led_fan_->get_name().c_str()   : "NOT SET");
  ESP_LOGCONFIG(TAG, "    Error: %s", led_error_ ? led_error_->get_name().c_str() : "NOT SET");
  ESP_LOGCONFIG(TAG, "  Mode select: %s", mode_select_ ? mode_select_->get_name().c_str() : "NOT SET");
}

// ============================================================================
// PASS 1: Zone State Calculation
// ============================================================================
void OpenZoningController::pass1_calc_zone_states_() {
  zone_error_flag_ = false;

  for (uint8_t i = 0; i < num_zones_; i++) {
    if (!zones_[i].enabled)
      continue;

    bool error = zones_[i].calc_state();
    if (error) {
      zone_error_flag_ = true;
    }
  }
}

// ============================================================================
// PASS 1.5: Short Cycle Protection
// ============================================================================
void OpenZoningController::pass1_5_short_cycle_protection_() {
  unsigned long current_time = millis();

  for (uint8_t i = 0; i < num_zones_; i++) {
    if (!zones_[i].enabled)
      continue;

    zones_[i].apply_short_cycle_protection(current_time, min_cycle_time_ms_);
  }
}

// ============================================================================
// PASS 2: Intelligent Multi-Zone Purge Management
// ============================================================================
void OpenZoningController::pass2_purge_management_() {
  unsigned long now_ms = millis();

  // Count how many zones are CURRENTLY (in new state) heating or cooling
  int heating_zones_count = 0;
  int cooling_zones_count = 0;

  for (uint8_t i = 0; i < num_zones_; i++) {
    if (!zones_[i].enabled)
      continue;
    if (zones_[i].is_heating()) heating_zones_count++;
    if (zones_[i].is_cooling()) cooling_zones_count++;
  }

  // Apply purge logic to each zone
  for (uint8_t i = 0; i < num_zones_; i++) {
    if (!zones_[i].enabled)
      continue;

    Zone &z = zones_[i];

    // Check transition: was active, now wants to stop
    if ((z.was_heating() || z.was_cooling()) && !z.is_heating() && !z.is_cooling()
        && z.state_new != ZoneState::ERROR) {

      // Check minimum cycle time before allowing purge
      unsigned long elapsed = (z.active_start_ms > 0) ? (now_ms - z.active_start_ms) : min_cycle_time_ms_;
      if (z.active_start_ms > 0 && elapsed < min_cycle_time_ms_) {
        z.state_new = z.state;  // Hold in previous state
        z.short_cycle_protection = true;
        ESP_LOGW(TAG, "Zone %d prevented from entering purge - minimum cycle time not met", i + 1);
      } else {
        // Check if other zones of the same type are still active
        bool other_zones_active = false;
        if (z.was_heating() && heating_zones_count > 0) other_zones_active = true;
        if (z.was_cooling() && cooling_zones_count > 0) other_zones_active = true;

        if (other_zones_active) {
          // Other zones still running — skip purge, go OFF
          z.purge_end_ms = 0;
          z.state_new = ZoneState::OFF;
        } else {
          // Last zone to stop — start purge timer
          z.purge_end_ms = now_ms + purge_duration_ms_;
          ESP_LOGI(TAG, "Zone %d starting purge (duration: %u ms)", i + 1, purge_duration_ms_);
        }
      }
    }

    // Manage active purge timer
    if (z.purge_end_ms > now_ms && z.state_new != ZoneState::ERROR) {
      z.state_new = ZoneState::PURGE;
    } else if (z.purge_end_ms <= now_ms && z.purge_end_ms != 0) {
      z.purge_end_ms = 0;
      ESP_LOGI(TAG, "Zone %d purge complete", i + 1);
    }
  }
}

// ============================================================================
// PASS 3: Priority Analysis and Wait States
// ============================================================================
void OpenZoningController::pass3_priority_analysis_() {
  // Calculate global maximum priority
  global_max_priority_ = 0;

  for (uint8_t i = 0; i < num_zones_; i++) {
    if (!zones_[i].enabled)
      continue;

    int p = zones_[i].get_priority();
    if (p > global_max_priority_) {
      global_max_priority_ = p;
    }
  }

  // Apply WAIT state to zones with lower priority (but not OFF or ERROR)
  for (uint8_t i = 0; i < num_zones_; i++) {
    if (!zones_[i].enabled)
      continue;

    int p = zones_[i].get_priority();
    if (p > 0 && p < global_max_priority_ && zones_[i].state_new != ZoneState::ERROR) {
      zones_[i].state_new = ZoneState::WAIT;
    }
  }
}

// ============================================================================
// PASS 4: Damper Control
// ============================================================================
void OpenZoningController::pass4_damper_control_() {
  bool all_zones_off = (global_max_priority_ == 0);
  uint8_t stagger_count = 0;

  for (uint8_t i = 0; i < num_zones_; i++) {
    if (!zones_[i].enabled)
      continue;

    Zone &z = zones_[i];

    // Determine target damper position
    uint8_t damper_target = 1;  // Default: open
    if (z.state_new == ZoneState::WAIT || z.state_new == ZoneState::ERROR) {
      damper_target = 0;  // Close for WAIT and ERROR
    } else if (all_zones_off) {
      damper_target = 1;  // All zones off: keep all dampers open
    } else if (z.state_new == ZoneState::OFF) {
      damper_target = 0;  // Zone off while others active: close
    } else {
      damper_target = 1;  // Active zone: open
    }

    // Apply damper change if needed
    if (damper_target != z.damper_state) {
      z.damper_state = damper_target;
      uint32_t offset = stagger_count * 100;  // 100ms between each zone to avoid MCP23017 I2C collisions
      if (damper_target == 1) {
        ESP_LOGI(TAG, "Zone %d damper opening (offset: %ums)", i + 1, offset);
        open_damper_(i, offset);
      } else {
        ESP_LOGI(TAG, "Zone %d damper closing (offset: %ums)", i + 1, offset);
        close_damper_(i, offset);
      }
      stagger_count++;
    }
  }
}

// ============================================================================
// Damper helpers — replaces the 12 ESPHome scripts
// Uses set_timeout() for the 250ms motor release delay
// ============================================================================
void OpenZoningController::open_damper_(uint8_t zone, uint32_t delay_offset) {
  if (zone >= num_zones_) return;
  Zone &z = zones_[zone];
  if (!z.damper_open_sw || !z.damper_close_sw) return;

  // Capture switch pointers for lambda
  switch_::Switch *open_sw = z.damper_open_sw;
  switch_::Switch *close_sw = z.damper_close_sw;

  // Step 1: after delay_offset, turn off both directions
  // Staggering avoids MCP23017 I2C register write collisions
  char tn_stop[20];
  snprintf(tn_stop, sizeof(tn_stop), "damper_s_%d", zone);
  this->set_timeout(tn_stop, delay_offset, [close_sw, open_sw]() {
    close_sw->turn_off();
    open_sw->turn_off();
  });

  // Step 2: after delay_offset + 250ms, engage the open direction
  char tn_open[20];
  snprintf(tn_open, sizeof(tn_open), "damper_o_%d", zone);
  this->set_timeout(tn_open, delay_offset + 250, [open_sw]() {
    open_sw->turn_on();
  });
}

void OpenZoningController::close_damper_(uint8_t zone, uint32_t delay_offset) {
  if (zone >= num_zones_) return;
  Zone &z = zones_[zone];
  if (!z.damper_open_sw || !z.damper_close_sw) return;

  // Capture switch pointers for lambda
  switch_::Switch *open_sw = z.damper_open_sw;
  switch_::Switch *close_sw = z.damper_close_sw;

  // Step 1: after delay_offset, turn off both directions
  char tn_stop[20];
  snprintf(tn_stop, sizeof(tn_stop), "damper_s_%d", zone);
  this->set_timeout(tn_stop, delay_offset, [open_sw, close_sw]() {
    open_sw->turn_off();
    close_sw->turn_off();
  });

  // Step 2: after delay_offset + 250ms, engage the close direction
  char tn_close[20];
  snprintf(tn_close, sizeof(tn_close), "damper_c_%d", zone);
  this->set_timeout(tn_close, delay_offset + 250, [close_sw]() {
    close_sw->turn_on();
  });
}

// ============================================================================
// PASS 5: Central Unit Output Control
// ============================================================================
void OpenZoningController::pass5_output_control_() {
  if (!auto_mode_) {
    return;  // Manual mode — don't touch outputs
  }

  int new_mode = 0;  // Default: Arrêt (index 0)

  // --- Error handling: force shutdown on zone error ---
  if (zone_error_flag_) {
    new_mode = 0;  // Arrêt
    if (led_error_) led_error_->turn_on();
    ESP_LOGE(TAG, "Zone error detected - forcing central unit to Arrêt");
  } else {
    if (led_error_) led_error_->turn_off();

    // --- Determine base mode from global_max_priority ---
    if (global_max_priority_ == 0) {
      new_mode = 0;  // Arrêt

    } else if (global_max_priority_ == 1) {
      new_mode = 1;  // Fan1

    } else if (global_max_priority_ == 2) {
      // Cooling demand — check if any zone needs stage 2
      bool needs_stage2 = false;
      for (uint8_t i = 0; i < num_zones_; i++) {
        if (zones_[i].state_new == ZoneState::COOLING_STAGE2) {
          needs_stage2 = true;
          break;
        }
      }
      new_mode = needs_stage2 ? 3 : 2;  // Clim Stage 2 or 1
      last_active_mode_ = 2;  // cooling

    } else if (global_max_priority_ == 4) {
      // Heating demand — check if any zone needs stage 2
      bool needs_stage2 = false;
      for (uint8_t i = 0; i < num_zones_; i++) {
        if (zones_[i].state_new == ZoneState::HEATING_STAGE2) {
          needs_stage2 = true;
          break;
        }
      }
      new_mode = needs_stage2 ? 5 : 4;  // Chauffage Stage 2 or 1
      last_active_mode_ = 1;  // heating

    } else if (global_max_priority_ == 6) {
      // Purge demand — fan only, preserve OB position from last active mode
      new_mode = (last_active_mode_ == 2) ? 7 : 6;
      // 7 = Purge Clim (G ON, OB ON), 6 = Purge Chauffage (G ON, OB OFF)
    }

    // --- Stage 2 escalation timer ---
    if (new_mode == 2 || new_mode == 4) {
      // Currently in Stage 1 — check escalation timer
      if (current_mode_ != 2 && current_mode_ != 4) {
        // Just entered Stage 1 — start timer
        stage1_start_ms_ = millis();
        ESP_LOGI(TAG, "Stage 1 started - escalation timer armed (%u ms)", stage2_escalation_ms_);
      } else if (stage2_escalation_ms_ > 0) {
        // Already in Stage 1 — check if escalation delay exceeded
        unsigned long stage1_elapsed = millis() - stage1_start_ms_;
        if (stage1_elapsed >= stage2_escalation_ms_) {
          new_mode = new_mode + 1;  // 2→3 (Clim S2) or 4→5 (Chauffage S2)
          ESP_LOGW(TAG, "Stage 2 ESCALATION triggered after %lu ms (threshold: %u ms)",
                   stage1_elapsed, stage2_escalation_ms_);
        }
      }
    } else {
      // Not in Stage 1 — reset escalation timer
      stage1_start_ms_ = 0;
    }
  }

  // --- Apply mode change via select entity ---
  if (new_mode != current_mode_) {
    ESP_LOGI(TAG, "Mode change: %d -> %d (priority: %d)", current_mode_, new_mode, global_max_priority_);
    current_mode_ = new_mode;
    apply_mode_(new_mode);
  }
}

// ============================================================================
// Apply mode — drives LEDs and outputs, syncs the select entity
// Replaces the on_value lambda in select.yml
// ============================================================================
void OpenZoningController::apply_mode_(int mode) {
  // Sync the select entity to reflect the new mode in HA
  if (mode_select_) {
    auto call = mode_select_->make_call();
    call.set_index(mode);
    call.perform();
  }

  // Default: all off
  bool y1 = false, y2 = false, g = false, ob = false;
  bool w1e = false, w2 = false, w3 = false;
  bool l_fan = false, l_heat = false, l_cool = false, l_error = false;

  switch (mode) {
    case 0:  // Arrêt
      ESP_LOGD(TAG, "Mode: Arrêt");
      break;
    case 1:  // Fan1
      ESP_LOGD(TAG, "Mode: Fan");
      l_fan = true;
      g = true;
      break;
    case 2:  // Clim Stage 1
      ESP_LOGD(TAG, "Mode: Clim Stage 1");
      l_fan = true; l_cool = true;
      y1 = true; g = true; ob = true;
      break;
    case 3:  // Clim Stage 2
      ESP_LOGD(TAG, "Mode: Clim Stage 2");
      l_fan = true; l_cool = true;
      y1 = true; y2 = true; g = true; ob = true;
      break;
    case 4:  // Chauffage Stage 1
      ESP_LOGD(TAG, "Mode: Chauffage Stage 1");
      l_fan = true; l_heat = true;
      y1 = true; g = true;
      break;
    case 5:  // Chauffage Stage 2
      ESP_LOGD(TAG, "Mode: Chauffage Stage 2");
      l_fan = true; l_heat = true;
      y1 = true; y2 = true; g = true;
      break;
    case 6:  // Purge Chauffage (fan only, OB OFF)
      ESP_LOGD(TAG, "Mode: Purge Chauffage");
      l_fan = true;
      g = true;
      break;
    case 7:  // Purge Clim (fan only, OB ON)
      ESP_LOGD(TAG, "Mode: Purge Clim");
      l_fan = true;
      g = true; ob = true;
      break;
    default:
      ESP_LOGW(TAG, "Unknown mode index: %d", mode);
      break;
  }

  // Apply outputs
  if (out_y1_)  { if (y1)  out_y1_->turn_on();  else out_y1_->turn_off();  }
  if (out_y2_)  { if (y2)  out_y2_->turn_on();  else out_y2_->turn_off();  }
  if (out_g_)   { if (g)   out_g_->turn_on();   else out_g_->turn_off();   }
  if (out_ob_)  { if (ob)  out_ob_->turn_on();  else out_ob_->turn_off();  }
  if (out_w1e_) { if (w1e) out_w1e_->turn_on(); else out_w1e_->turn_off(); }
  if (out_w2_)  { if (w2)  out_w2_->turn_on();  else out_w2_->turn_off();  }
  if (out_w3_)  { if (w3)  out_w3_->turn_on();  else out_w3_->turn_off();  }

  // Apply LEDs
  if (led_fan_)   { if (l_fan)   led_fan_->turn_on();   else led_fan_->turn_off();   }
  if (led_heat_)  { if (l_heat)  led_heat_->turn_on();  else led_heat_->turn_off();  }
  if (led_cool_)  { if (l_cool)  led_cool_->turn_on();  else led_cool_->turn_off();  }
  if (led_error_) { if (l_error) led_error_->turn_on(); else led_error_->turn_off(); }
}

}  // namespace open_zoning
}  // namespace esphome
