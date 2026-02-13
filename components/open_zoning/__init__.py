import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID

CODEOWNERS = ["@jlacasse"]
DEPENDENCIES = []

# Namespace matching the C++ namespace
open_zoning_ns = cg.esphome_ns.namespace("open_zoning")
OpenZoningController = open_zoning_ns.class_(
    "OpenZoningController", cg.PollingComponent
)

# Configuration keys
CONF_ZONES = "zones"
CONF_Y1 = "y1"
CONF_Y2 = "y2"
CONF_G = "g"
CONF_OB = "ob"
CONF_MIN_CYCLE_TIME = "min_cycle_time"
CONF_PURGE_DURATION = "purge_duration"

# Per-zone schema: references to thermostat input binary sensors
ZONE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_Y1): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_Y2): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_G): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_OB): cv.use_id(binary_sensor.BinarySensor),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenZoningController),
        cv.Required(CONF_ZONES): cv.All(
            cv.ensure_list(ZONE_SCHEMA),
            cv.Length(min=1, max=6),
        ),
        cv.Optional(CONF_MIN_CYCLE_TIME, default="480s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_PURGE_DURATION, default="300s"): cv.positive_time_period_milliseconds,
    }
).extend(cv.polling_component_schema("10s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set number of zones
    zones = config[CONF_ZONES]
    cg.add(var.set_num_zones(len(zones)))

    # Set configuration parameters
    cg.add(var.set_min_cycle_time(config[CONF_MIN_CYCLE_TIME]))
    cg.add(var.set_purge_duration(config[CONF_PURGE_DURATION]))

    # Register binary sensor references for each zone
    for i, zone_conf in enumerate(zones):
        y1 = await cg.get_variable(zone_conf[CONF_Y1])
        y2 = await cg.get_variable(zone_conf[CONF_Y2])
        g = await cg.get_variable(zone_conf[CONF_G])
        ob = await cg.get_variable(zone_conf[CONF_OB])
        cg.add(var.set_zone_sensors(i, y1, y2, g, ob))
