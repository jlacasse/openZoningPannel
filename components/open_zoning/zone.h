#pragma once

#include <cstdint>
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace open_zoning {

/// Zone operating states — matches the original #define values
enum class ZoneState : uint8_t {
  OFF = 0,
  FAN_ONLY = 1,
  COOLING_STAGE1 = 2,
  COOLING_STAGE2 = 3,
  HEATING_STAGE1 = 4,
  HEATING_STAGE2 = 5,
  PURGE = 6,
  WAIT = 7,
  ERROR = 99
};

/// Returns the priority level for a given zone state
/// PURGE(6) > HEATING(4) > COOLING(2) > FAN(1) > OFF/WAIT/ERROR(0)
inline int state_to_priority(ZoneState state) {
  switch (state) {
    case ZoneState::PURGE:
      return 6;
    case ZoneState::HEATING_STAGE1:
    case ZoneState::HEATING_STAGE2:
      return 4;
    case ZoneState::COOLING_STAGE1:
    case ZoneState::COOLING_STAGE2:
      return 2;
    case ZoneState::FAN_ONLY:
      return 1;
    default:
      return 0;
  }
}

/// Returns a human-readable string for a zone state (for text sensors)
inline const char *state_to_string(ZoneState state) {
  switch (state) {
    case ZoneState::OFF:
      return "Off";
    case ZoneState::FAN_ONLY:
      return "Fan Only";
    case ZoneState::COOLING_STAGE1:
      return "Cooling Stage 1";
    case ZoneState::COOLING_STAGE2:
      return "Cooling Stage 2";
    case ZoneState::HEATING_STAGE1:
      return "Heating Stage 1";
    case ZoneState::HEATING_STAGE2:
      return "Heating Stage 2";
    case ZoneState::PURGE:
      return "Purge";
    case ZoneState::WAIT:
      return "Wait";
    case ZoneState::ERROR:
      return "ERROR";
    default:
      return "Unknown";
  }
}

/// Per-zone state container — holds all runtime data for one zone
struct Zone {
  uint8_t index{0};  // Zone number (0-based internally, 1-based for logging)

  // --- Thermostat input sensors (set via codegen from __init__.py) ---
  binary_sensor::BinarySensor *y1{nullptr};
  binary_sensor::BinarySensor *y2{nullptr};
  binary_sensor::BinarySensor *g{nullptr};
  binary_sensor::BinarySensor *ob{nullptr};

  // --- Damper output switches (set via codegen from __init__.py) ---
  switch_::Switch *damper_open_sw{nullptr};
  switch_::Switch *damper_close_sw{nullptr};

  // Current and next computed state
  ZoneState state{ZoneState::OFF};
  ZoneState state_new{ZoneState::OFF};

  // Error detection
  uint8_t error_count{0};

  // Damper tracking
  uint8_t damper_state{255};  // 255 = unknown (forces first update to drive correct position), 1 = open, 0 = closed

  // Purge timer
  unsigned long purge_end_ms{0};

  // Short cycle protection
  unsigned long active_start_ms{0};
  bool short_cycle_protection{false};

  // Zone enable flag (for future optimization #2)
  bool enabled{true};

  // --- PASS 1: Calculate zone state from thermostat inputs ---
  // Returns true if this zone triggered an error
  bool calc_state();

  // --- PASS 1.5: Short cycle protection ---
  void apply_short_cycle_protection(unsigned long current_time, unsigned long min_cycle_time_ms);

  // --- PASS 3: Priority ---
  int get_priority() const { return state_to_priority(state_new); }

  // --- Helper methods ---
  bool is_heating() const {
    return state_new == ZoneState::HEATING_STAGE1 || state_new == ZoneState::HEATING_STAGE2;
  }
  bool is_cooling() const {
    return state_new == ZoneState::COOLING_STAGE1 || state_new == ZoneState::COOLING_STAGE2;
  }
  bool was_heating() const {
    return state == ZoneState::HEATING_STAGE1 || state == ZoneState::HEATING_STAGE2;
  }
  bool was_cooling() const {
    return state == ZoneState::COOLING_STAGE1 || state == ZoneState::COOLING_STAGE2;
  }
  bool is_active() const { return is_heating() || is_cooling(); }
  bool was_active() const { return was_heating() || was_cooling(); }
};

}  // namespace open_zoning
}  // namespace esphome
