#include "open_zoning.h"

namespace esphome {
namespace open_zoning {

void OpenZoningController::setup() {
  ESP_LOGI(TAG, "OpenZoning initialized — %d zones configured", MAX_ZONES);

  // Initialize all zones with their index
  for (uint8_t i = 0; i < MAX_ZONES; i++) {
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
  // Phase 0A: skeleton — log heartbeat only
  ESP_LOGD(TAG, "OpenZoning update cycle (heartbeat)");
}

void OpenZoningController::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenZoning Controller:");
  ESP_LOGCONFIG(TAG, "  Update interval: %.1fs", this->get_update_interval() / 1000.0f);
  ESP_LOGCONFIG(TAG, "  Max zones: %d", MAX_ZONES);
}

}  // namespace open_zoning
}  // namespace esphome
