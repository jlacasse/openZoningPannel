#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
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

 protected:
  Zone zones_[MAX_ZONES];
};

}  // namespace open_zoning
}  // namespace esphome
