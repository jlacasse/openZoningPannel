import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, switch, select, text_sensor
from esphome.const import CONF_ID

CODEOWNERS = ["@jlacasse"]
DEPENDENCIES = []

# Namespace matching the C++ namespace
open_zoning_ns = cg.esphome_ns.namespace("open_zoning")
OpenZoningController = open_zoning_ns.class_(
    "OpenZoningController", cg.PollingComponent
)

# Configuration keys — zones
CONF_ZONES = "zones"
CONF_Y1 = "y1"
CONF_Y2 = "y2"
CONF_G = "g"
CONF_OB = "ob"
CONF_DAMPER_OPEN = "damper_open"
CONF_DAMPER_CLOSE = "damper_close"
CONF_STATE_SENSOR = "state_sensor"

# Configuration keys — timing
CONF_MIN_CYCLE_TIME = "min_cycle_time"
CONF_PURGE_DURATION = "purge_duration"
CONF_STAGE2_ESCALATION_DELAY = "stage2_escalation_delay"

# Configuration keys — outputs
CONF_OUT_Y1 = "out_y1"
CONF_OUT_Y2 = "out_y2"
CONF_OUT_G = "out_g"
CONF_OUT_OB = "out_ob"
CONF_OUT_W1E = "out_w1e"
CONF_OUT_W2 = "out_w2"
CONF_OUT_W3 = "out_w3"

# Configuration keys — LEDs
CONF_LED_HEAT = "led_heat"
CONF_LED_COOL = "led_cool"
CONF_LED_FAN = "led_fan"
CONF_LED_ERROR = "led_error"

# Configuration keys — select
CONF_MODE_SELECT = "mode_select"
CONF_AUTO_MODE = "auto_mode"

# Per-zone schema: thermostat inputs + damper switches
ZONE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_Y1): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_Y2): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_G): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_OB): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_DAMPER_OPEN): cv.use_id(switch.Switch),
        cv.Required(CONF_DAMPER_CLOSE): cv.use_id(switch.Switch),
        cv.Optional(CONF_STATE_SENSOR): cv.use_id(text_sensor.TextSensor),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenZoningController),
        cv.Required(CONF_ZONES): cv.All(
            cv.ensure_list(ZONE_SCHEMA),
            cv.Length(min=1, max=6),
        ),
        # Timing
        cv.Optional(CONF_MIN_CYCLE_TIME, default="480s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_PURGE_DURATION, default="300s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_STAGE2_ESCALATION_DELAY, default="3600s"): cv.positive_time_period_milliseconds,
        # Central unit outputs
        cv.Required(CONF_OUT_Y1): cv.use_id(switch.Switch),
        cv.Required(CONF_OUT_Y2): cv.use_id(switch.Switch),
        cv.Required(CONF_OUT_G): cv.use_id(switch.Switch),
        cv.Required(CONF_OUT_OB): cv.use_id(switch.Switch),
        cv.Optional(CONF_OUT_W1E): cv.use_id(switch.Switch),
        cv.Optional(CONF_OUT_W2): cv.use_id(switch.Switch),
        cv.Optional(CONF_OUT_W3): cv.use_id(switch.Switch),
        # LED indicators
        cv.Required(CONF_LED_HEAT): cv.use_id(switch.Switch),
        cv.Required(CONF_LED_COOL): cv.use_id(switch.Switch),
        cv.Required(CONF_LED_FAN): cv.use_id(switch.Switch),
        cv.Required(CONF_LED_ERROR): cv.use_id(switch.Switch),
        # Mode select
        cv.Required(CONF_MODE_SELECT): cv.use_id(select.Select),
        cv.Optional(CONF_AUTO_MODE, default=True): cv.boolean,
    }
).extend(cv.polling_component_schema("10s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set number of zones
    zones = config[CONF_ZONES]
    cg.add(var.set_num_zones(len(zones)))

    # Set timing parameters
    cg.add(var.set_min_cycle_time(config[CONF_MIN_CYCLE_TIME]))
    cg.add(var.set_purge_duration(config[CONF_PURGE_DURATION]))
    cg.add(var.set_stage2_escalation_delay(config[CONF_STAGE2_ESCALATION_DELAY]))
    cg.add(var.set_auto_mode(config[CONF_AUTO_MODE]))

    # Register binary sensor and switch references for each zone
    for i, zone_conf in enumerate(zones):
        y1 = await cg.get_variable(zone_conf[CONF_Y1])
        y2 = await cg.get_variable(zone_conf[CONF_Y2])
        g = await cg.get_variable(zone_conf[CONF_G])
        ob = await cg.get_variable(zone_conf[CONF_OB])
        cg.add(var.set_zone_sensors(i, y1, y2, g, ob))

        damper_open = await cg.get_variable(zone_conf[CONF_DAMPER_OPEN])
        damper_close = await cg.get_variable(zone_conf[CONF_DAMPER_CLOSE])
        cg.add(var.set_zone_dampers(i, damper_open, damper_close))

        if CONF_STATE_SENSOR in zone_conf:
            state_sensor = await cg.get_variable(zone_conf[CONF_STATE_SENSOR])
            cg.add(var.set_zone_state_sensor(i, state_sensor))

    # Central unit outputs
    out_y1 = await cg.get_variable(config[CONF_OUT_Y1])
    cg.add(var.set_out_y1(out_y1))
    out_y2 = await cg.get_variable(config[CONF_OUT_Y2])
    cg.add(var.set_out_y2(out_y2))
    out_g = await cg.get_variable(config[CONF_OUT_G])
    cg.add(var.set_out_g(out_g))
    out_ob = await cg.get_variable(config[CONF_OUT_OB])
    cg.add(var.set_out_ob(out_ob))

    if CONF_OUT_W1E in config:
        out_w1e = await cg.get_variable(config[CONF_OUT_W1E])
        cg.add(var.set_out_w1e(out_w1e))
    if CONF_OUT_W2 in config:
        out_w2 = await cg.get_variable(config[CONF_OUT_W2])
        cg.add(var.set_out_w2(out_w2))
    if CONF_OUT_W3 in config:
        out_w3 = await cg.get_variable(config[CONF_OUT_W3])
        cg.add(var.set_out_w3(out_w3))

    # LED indicators
    led_heat = await cg.get_variable(config[CONF_LED_HEAT])
    cg.add(var.set_led_heat(led_heat))
    led_cool = await cg.get_variable(config[CONF_LED_COOL])
    cg.add(var.set_led_cool(led_cool))
    led_fan = await cg.get_variable(config[CONF_LED_FAN])
    cg.add(var.set_led_fan(led_fan))
    led_error = await cg.get_variable(config[CONF_LED_ERROR])
    cg.add(var.set_led_error(led_error))

    # Mode select entity
    mode_select = await cg.get_variable(config[CONF_MODE_SELECT])
    cg.add(var.set_mode_select(mode_select))
