#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/preferences.h"
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
  void loop() override;
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
  void set_zone_state_sensor(uint8_t index, text_sensor::TextSensor *sensor);
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

  // --- I2C watchdog setters ---
  void set_i2c_bus(i2c::I2CBus *bus) { i2c_bus_ = bus; }
  void set_i2c_health_sensor(binary_sensor::BinarySensor *s) { i2c_health_sensor_ = s; }
  void set_i2c_error_threshold(uint8_t n) { i2c_error_threshold_ = n; }

  // --- Minimum zone demand setters ---
  void set_min_active_zones(uint8_t n) { min_active_zones_ = n; }
  void set_min_demand_override_delay(uint32_t ms) { min_demand_override_ms_ = ms; }

  // --- Zone enable/disable (optimization #2) ---
  void set_zone_enabled(uint8_t index, bool enabled) {
    if (index < num_zones_) zones_[index].enabled = enabled;
  }

  // --- O/B polarity per zone (optimization #12) ---
  // on_heat=true  : O/B active → heating (default)
  // on_heat=false : O/B active → cooling
  void set_zone_ob_on_heat(uint8_t index, bool on_heat) {
    if (index < num_zones_) zones_[index].ob_on_heat = on_heat;
  }

  // --- Optimization #3: diagnostic sensor setters ---
  void set_active_zones_sensor(sensor::Sensor *s)       { active_zones_sensor_ = s; }
  void set_stage1_elapsed_sensor(sensor::Sensor *s)     { stage1_elapsed_sensor_ = s; }
  void set_mode_changes_sensor(sensor::Sensor *s)       { mode_changes_sensor_ = s; }
  void set_short_cycle_sensor(binary_sensor::BinarySensor *s) { short_cycle_sensor_ = s; }

  // --- Optimization #10: anti-conflict select guard ---
  // Immediately re-applies the component's current mode, overriding any manual
  // select change made from Home Assistant while auto_mode is active.
  void reapply_mode() { apply_mode_(current_mode_); }
  // Returns true while the component itself is driving the select entity,
  // allowing on_value callbacks to distinguish component vs. user changes.
  bool is_component_driving_select() const { return component_driving_select_; }

  // --- Runtime getters (for template entities in YAML) ---
  bool get_auto_mode() const { return auto_mode_; }
  uint32_t get_min_cycle_time_ms() const { return min_cycle_time_ms_; }
  uint32_t get_purge_duration_ms() const { return purge_duration_ms_; }
  uint8_t get_min_active_zones() const { return min_active_zones_; }

 protected:
  // --- Pass methods ---
  void pass1_calc_zone_states_();
  void pass1_5_short_cycle_protection_();
  void pass2_purge_management_();
  void pass2_5_minimum_demand_();
  void pass3_priority_analysis_();
  void pass4_damper_control_();
  void pass5_output_control_();
  void check_i2c_health_();
  void publish_diagnostics_();  // Optimization #3

  // --- Damper operation queue ---
  // Each damper change is split into 3 individual I2C operations
  // processed one-per-loop-iteration for ESP8266 I2C reliability.
  // This mimics how the old ESPHome scripts worked (yield between each action).
  struct DamperOp {
    switch_::Switch *sw;
    bool turn_on;
    uint32_t delay_ms;  // ms to wait after previous op before executing
  };
  static constexpr uint8_t MAX_DAMPER_OPS = MAX_ZONES * 3;  // 3 steps per damper change
  DamperOp damper_ops_[MAX_DAMPER_OPS];
  uint8_t dq_count_{0};
  uint8_t dq_pos_{0};
  unsigned long dq_next_ms_{0};

  void queue_open_damper_(uint8_t zone);
  void queue_close_damper_(uint8_t zone);

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

  // --- I2C watchdog ---
  i2c::I2CBus *i2c_bus_{nullptr};
  binary_sensor::BinarySensor *i2c_health_sensor_{nullptr};
  uint8_t i2c_error_count_{0};
  uint8_t i2c_error_threshold_{3};
  bool i2c_healthy_{true};

  // --- Minimum zone demand ---
  uint8_t min_active_zones_{1};           // 1 = disabled (all single requests allowed)
  uint32_t min_demand_override_ms_{1800000}; // 30 min emergency override
  unsigned long min_demand_wait_start_ms_{0};

  // --- Runtime state ---
  bool zone_error_flag_{false};
  int global_max_priority_{0};
  int current_mode_{0};       // Tracks active mode index (0-7)
  int last_active_mode_{0};   // 0=unknown, 1=heating, 2=cooling
  bool auto_mode_{true};      // Auto mode enabled by default
  unsigned long stage1_start_ms_{0};  // Stage 2 escalation timer
  bool component_driving_select_{false};  // Optimization #10: true while component drives the select
  ESPPreferenceObject last_active_mode_pref_;  // Optimization #5: flash persistence

  // --- Optimization #3: diagnostic sensors ---
  sensor::Sensor *active_zones_sensor_{nullptr};
  sensor::Sensor *stage1_elapsed_sensor_{nullptr};
  sensor::Sensor *mode_changes_sensor_{nullptr};
  binary_sensor::BinarySensor *short_cycle_sensor_{nullptr};
  uint32_t mode_change_count_{0};  // incremented at each real mode transition
};

}  // namespace open_zoning
}  // namespace esphome
