#include <mod_template.h>

#include <core/config.h>
#include <core/logger.h>
#include <scripting/lua_binding.h>
#include <scripting/lua_engine.h>
#include <core/module_registry.h>
#include <server/router.h>
#include <core/service_registry.h>

#include <crow.h>

namespace api
{

template_service::template_service(std::string service_name)
    : name_(std::move(service_name))
{
}

const std::string &template_service::name() const { return name_; }

void template_service::set_name(const std::string &service_name)
{
    name_ = service_name;
}

int template_service::calculate(int first_number, int second_number) const
{
    return first_number + second_number;
}

bool template_service::process_item(const TemplateData &item)
{
    LOG_INFO("mod_template", "Processing item: " << item.id << " (priority: "
                                                 << item.priority << ")");

    return !item.id.empty();
}

mod_template::mod_template()
    : module("mod_template", "1.0.0",
             "Template module demonstrating class, struct, namespace, and "
             "variable Lua bindings")
{
}

void mod_template::on_config_load(ServerConfig & /*configuration*/) {}

void mod_template::on_init(const ServerConfig & /*configuration*/)
{
    LOG_INFO("mod_template", "Initialized");
}

void mod_template::on_handlers_register(router & /*server_router*/) {}

void mod_template::on_lua_init(lua_engine &lua_engine_instance)
{
    std::lock_guard<std::mutex> lock(lua_engine_instance.mutex());
    auto &lua_state = lua_engine_instance.state();

    lua_state.new_usertype<TemplateData>(
        "template_data", sol::constructors<TemplateData()>(), "id",
        &TemplateData::id, "value", &TemplateData::value, "priority",
        &TemplateData::priority);

    lua_state.new_usertype<template_service>(
        "template_service",
        sol::constructors<template_service(), template_service(std::string)>(),
        "name",
        sol::property(&template_service::name, &template_service::set_name),
        "calculate", &template_service::calculate, "process_item",
        &template_service::process_item);

    sol::table module_namespace =
        lua_get_or_create_namespace(lua_state, "mod_template");

    module_namespace["template_data"] = lua_state["template_data"];
    module_namespace["template_service"] = lua_state["template_service"];

    module_namespace["version"] = "1.0.0";
    module_namespace["active_service"] = &default_service_;

    module_namespace["quick_calc"] = [](int first_factor,
                                        int second_factor) -> int
    { return first_factor * second_factor; };

    LOG_INFO("mod_template",
             "Registered Lua bindings: structs, classes, variables, and "
             "functions in 'mod_template' namespace");
}

bool mod_template::on_request_override(crow::request & /*request*/,
                                       crow::response & /*response*/
)
{
    return false;
}

void mod_template::on_before_request(crow::request & /*request*/,
                                     crow::response & /*response*/
)
{
}

void mod_template::on_after_request(crow::request & /*request*/,
                                    crow::response & /*response*/,
                                    double /*duration_milliseconds*/
)
{
}

void mod_template::on_server_startup(api_server & /*server_instance*/) {}

void mod_template::on_server_shutdown() {}

} // namespace api

API_REGISTER_MODULE(api::mod_template)
