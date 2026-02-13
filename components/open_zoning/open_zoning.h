#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"
#include "zone.h"

namespace esphome {
namespace open_zoning {

static const char *const TAG = "open_zoning";
static const uint8_t MAX_ZONES = 6;

class OpenZoningController : public PollingComponent {
 public:
  // --- PollingComponent overrides ---
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // --- Configuration setters (called from __init__.py codegen) ---
  void set_zone_sensors(uint8_t index,
                        binary_sensor::BinarySensor *y1,
                        binary_sensor::BinarySensor *y2,
                        binary_sensor::BinarySensor *g,
                        binary_sensor::BinarySensor *ob);
  void set_zone_dampers(uint8_t index,
                        switch_::Switch *damper_open,
                        switch_::Switch *damper_close);
  void set_num_zones(uint8_t n) { num_zones_ = n; }
  void set_min_cycle_time(uint32_t ms) { min_cycle_time_ms_ = ms; }
  void set_purge_duration(uint32_t ms) { purge_duration_ms_ = ms; }

  // --- Output / LED / Select setters ---
  void set_out_y1(switch_::Switch *sw) { out_y1_ = sw; }
  void set_out_y2(switch_::Switch *sw) { out_y2_ = sw; }
  void set_out_g(switch_::Switch *sw) { out_g_ = sw; }
  void set_out_ob(switch_::Switch *sw) { out_ob_ = sw; }
  void set_out_w1e(switch_::Switch *sw) { out_w1e_ = sw; }
  void set_out_w2(switch_::Switch *sw) { out_w2_ = sw; }
  void set_out_w3(switch_::Switch *sw) { out_w3_ = sw; }
  void set_led_heat(switch_::Switch *sw) { led_heat_ = sw; }
  void set_led_cool(switch_::Switch *sw) { led_cool_ = sw; }
  void set_led_fan(switch_::Switch *sw) { led_fan_ = sw; }
  void set_led_error(switch_::Switch *sw) { led_error_ = sw; }
  void set_mode_select(select::Select *sel) { mode_select_ = sel; }
  void set_auto_mode(bool v) { auto_mode_ = v; }
  void set_stage2_escalation_delay(uint32_t ms) { stage2_escalation_ms_ = ms; }

 protected:
  // --- Pass methods ---
  void pass1_calc_zone_states_();
  void pass1_5_short_cycle_protection_();
  void pass2_purge_management_();
  void pass3_priority_analysis_();
  void pass4_damper_control_();
  void pass5_output_control_();

  // --- Damper helpers (use set_timeout for 250ms motor release) ---
  void open_damper_(uint8_t zone);
  void close_damper_(uint8_t zone);

  // --- Central unit mode application ---
  void apply_mode_(int mode);

  // --- Zone data ---
  Zone zones_[MAX_ZONES];
  uint8_t num_zones_{0};

  // --- Configuration ---
  uint32_t min_cycle_time_ms_{480000};    // 8 minutes default
  uint32_t purge_duration_ms_{300000};    // 5 minutes default
  uint32_t stage2_escalation_ms_{3600000}; // 1 hour default

  // --- Central unit output switches ---
  switch_::Switch *out_y1_{nullptr};
  switch_::Switch *out_y2_{nullptr};
  switch_::Switch *out_g_{nullptr};
  switch_::Switch *out_ob_{nullptr};
  switch_::Switch *out_w1e_{nullptr};
  switch_::Switch *out_w2_{nullptr};
  switch_::Switch *out_w3_{nullptr};

  // --- LED indicator switches ---
  switch_::Switch *led_heat_{nullptr};
  switch_::Switch *led_cool_{nullptr};
  switch_::Switch *led_fan_{nullptr};
  switch_::Switch *led_error_{nullptr};

  // --- Mode select entity ---
  select::Select *mode_select_{nullptr};

  // --- Runtime state ---
  bool zone_error_flag_{false};
  int global_max_priority_{0};
  int current_mode_{0};       // Tracks active mode index (0-7)
  int last_active_mode_{0};   // 0=unknown, 1=heating, 2=cooling
  bool auto_mode_{true};      // Auto mode enabled by default
  unsigned long stage1_start_ms_{0};  // Stage 2 escalation timer
};

}  // namespace open_zoning
}  // namespace esphome
