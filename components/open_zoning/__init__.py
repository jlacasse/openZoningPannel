import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@jlacasse"]
DEPENDENCIES = []

# Namespace matching the C++ namespace
open_zoning_ns = cg.esphome_ns.namespace("open_zoning")
OpenZoningController = open_zoning_ns.class_(
    "OpenZoningController", cg.PollingComponent
)

# Phase 0A: minimal schema — just the component ID and update interval
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenZoningController),
    }
).extend(cv.polling_component_schema("10s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
