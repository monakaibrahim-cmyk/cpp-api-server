#include <mod_orm.h>

#include <core/config.h>
#include <core/logger.h>
#include <scripting/lua_engine.h>
#include <core/module_registry.h>
#include <core/orm.h>
#include <core/service_registry.h>

namespace api
{

mod_orm::mod_orm()
    : module("mod_orm", "1.0.0",
             "Pluggable Scaffold ORM module with overridable database driver "
             "architecture")
{
}

void mod_orm::on_config_load(ServerConfig & /*configuration*/) {}

void mod_orm::on_init(const ServerConfig &configuration)
{
    s_services().register_service<orm_engine>(
        "orm", std::shared_ptr<orm_engine>(&orm_engine::instance(),
                                           [](orm_engine *) {}));

    if (!configuration.db_driver.empty() && configuration.db_driver != "none")
    {
        if (s_orm().has_driver_factory(configuration.db_driver))
        {
            s_orm().set_active_driver(configuration.db_driver,
                                      configuration.db_connection);

            LOG_INFO("orm", "mod_orm initialized with active driver: "
                                << configuration.db_driver << " ("
                                << configuration.db_connection << ")");
        }
        else
        {
            LOG_WARN("orm", "Configured driver '"
                                << configuration.db_driver
                                << "' is not registered yet. Waiting for "
                                   "custom C++ module registration.");
        }
    }
    else
    {
        LOG_INFO("orm", "mod_orm initialized (template scaffold mode: ready "
                        "for custom C++ driver registration)");
    }
}

void mod_orm::on_lua_init(lua_engine &lua_engine_instance)
{
    std::lock_guard<std::mutex> lock(lua_engine_instance.mutex());

    s_orm().bind_lua(lua_engine_instance.state());

    LOG_INFO("orm", "mod_orm: DB interface bound to Lua state (DB.execute, "
                    "DB.query, DB.scaffold, DB.table)");
}

void mod_orm::on_server_shutdown()
{
    auto database_driver = s_orm().get_driver();

    if (database_driver)
    {
        database_driver->disconnect();
    }

    LOG_INFO("orm", "mod_orm: DB connections closed");
}

} // namespace api

API_REGISTER_MODULE(api::mod_orm)
