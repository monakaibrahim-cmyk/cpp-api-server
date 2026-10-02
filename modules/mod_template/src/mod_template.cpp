#include "mod_template.hpp"
#include "api/module_registry.hpp"
#include "api/logger.hpp"
#include "api/lua_engine.hpp"
#include "api/router.hpp"
#include "api/service_registry.hpp"

#include <crow.h>

namespace api
{

mod_template::mod_template()
    : module("mod_template", "1.0.0", "Template module demonstrating fluid overrides")
{
}

void mod_template::on_config_load(server_config& /*cfg*/)
{
}

void mod_template::on_init(const server_config& /*cfg*/)
{
    LOG_INFO("mod_template", "Initialized");
}

void mod_template::on_handlers_register(router& /*rtr*/)
{
}

void mod_template::on_lua_init(lua_engine& lua)
{
    std::lock_guard<std::mutex> lock(lua.mutex());
}

bool mod_template::on_request_override(crow::request& /*req*/, crow::response& /*res*/)
{
    return false;
}

void mod_template::on_before_request(crow::request& /*req*/, crow::response& /*res*/)
{
}

void mod_template::on_after_request(crow::request& /*req*/, crow::response& /*res*/, double /*duration_ms*/)
{
}

void mod_template::on_server_startup(api_server& /*server*/)
{
}

void mod_template::on_server_shutdown()
{
}

} // namespace api

API_REGISTER_MODULE(api::mod_template)
