#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
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
  void set_num_zones(uint8_t n) { num_zones_ = n; }
  void set_min_cycle_time(uint32_t ms) { min_cycle_time_ms_ = ms; }
  void set_purge_duration(uint32_t ms) { purge_duration_ms_ = ms; }

 protected:
  // --- Pass methods (PASS 1–3 for Phase 0C) ---
  void pass1_calc_zone_states_();
  void pass1_5_short_cycle_protection_();
  void pass2_purge_management_();
  void pass3_priority_analysis_();

  // --- Zone data ---
  Zone zones_[MAX_ZONES];
  uint8_t num_zones_{0};

  // --- Configuration ---
  uint32_t min_cycle_time_ms_{480000};   // 8 minutes default
  uint32_t purge_duration_ms_{300000};   // 5 minutes default

  // --- Runtime state ---
  bool zone_error_flag_{false};
  int global_max_priority_{0};
};

}  // namespace open_zoning
}  // namespace esphome
