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

void OpenZoningController::setup() {
  ESP_LOGI(TAG, "OpenZoning initialized — %d zones configured", num_zones_);

  // Initialize all configured zones with their index
  for (uint8_t i = 0; i < num_zones_; i++) {
    zones_[i].index = i;
    zones_[i].state = ZoneState::OFF;
    zones_[i].state_new = ZoneState::OFF;
    zones_[i].damper_state = 1;  // All dampers open on boot
    zones_[i].error_count = 0;
    zones_[i].purge_end_ms = 0;
    zones_[i].active_start_ms = 0;
    zones_[i].short_cycle_protection = false;
    zones_[i].enabled = true;
  }
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
  for (uint8_t i = 0; i < num_zones_; i++) {
    ESP_LOGCONFIG(TAG, "  Zone %d:", i + 1);
    ESP_LOGCONFIG(TAG, "    Y1: %s", zones_[i].y1 ? zones_[i].y1->get_name().c_str() : "NOT SET");
    ESP_LOGCONFIG(TAG, "    Y2: %s", zones_[i].y2 ? zones_[i].y2->get_name().c_str() : "NOT SET");
    ESP_LOGCONFIG(TAG, "    G:  %s", zones_[i].g  ? zones_[i].g->get_name().c_str()  : "NOT SET");
    ESP_LOGCONFIG(TAG, "    OB: %s", zones_[i].ob ? zones_[i].ob->get_name().c_str() : "NOT SET");
  }
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

}  // namespace open_zoning
}  // namespace esphome
