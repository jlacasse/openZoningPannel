#pragma once

#include <cstdint>

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

  // Current and next computed state
  ZoneState state{ZoneState::OFF};
  ZoneState state_new{ZoneState::OFF};

  // Error detection
  uint8_t error_count{0};

  // Damper tracking
  uint8_t damper_state{1};  // 1 = open, 0 = closed

  // Purge timer
  unsigned long purge_end_ms{0};

  // Short cycle protection
  unsigned long active_start_ms{0};
  bool short_cycle_protection{false};

  // Zone enable flag (for future optimization #2)
  bool enabled{true};
};

}  // namespace open_zoning
}  // namespace esphome
