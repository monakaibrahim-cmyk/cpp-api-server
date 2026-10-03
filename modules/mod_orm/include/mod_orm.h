#pragma once

#include "api/module.h"

namespace api
{

struct ServerConfig;
class lua_engine;

/**
 * @brief Dynamic ORM module providing Eloquent-style query builder and database
 * scaffolding.
 *
 * Implements an extension module (@ref module) that registers the core @ref
 * orm_engine into @ref service_registry, connects the configured database
 * driver upon server initialization, exposes the @c DB Lua interface into the
 * Lua runtime during bootstrap, and disconnects database drivers upon server
 * shutdown.
 *
 * @par C++ Module Integration Example
 * @code{.cpp}
 * // Retrieve ORM engine published by mod_orm through the service registry
 * auto orm_service = api::s_services().get_service<api::orm_engine>("orm");
 * if (orm_service && orm_service->has_driver())
 * {
 *     auto results = orm_service->table("users")
 *         .where("is_active", "=", 1)
 *         .order_by("created_at", "DESC")
 *         .limit(10)
 *         .get();
 * }
 * @endcode
 *
 * @par Lua Usage Example
 * @code{.lua}
 * -- In Lua route handlers or background scripts:
 * local users = DB.table("users")
 *     :where("status", "=", "active")
 *     :order_by("id", "DESC")
 *     :limit(25)
 *     :get()
 *
 * for _, user in ipairs(users) do
 *     print("User ID: " .. tostring(user.id) .. ", Name: " ..
 * tostring(user.name)) end
 * @endcode
 *
 * @thread_safety Concurrency is synchronized through the underlying @ref
 * orm_engine and active database driver connection pool mutexes.
 *
 * @headerfile mod_orm.h
 */
class mod_orm : public module
{
  public:
    /**
     * @brief Constructs the @ref mod_orm module descriptor with metadata.
     */
    mod_orm();

    /**
     * @brief Virtual destructor for clean module teardown.
     */
    ~mod_orm() override = default;

    /**
     * @brief Lifecycle hook called when server configuration is loaded.
     *
     * @param configuration Mutable server configuration.
     */
    void on_config_load(ServerConfig &configuration) override;

    /**
     * @brief Lifecycle hook called during module initialization to bind ORM
     * service and driver.
     *
     * Registers @ref orm_engine with the global @ref service_registry and
     * initializes the active database driver connection specified by @p
     * configuration.
     *
     * @param configuration Const reference to the active server configuration.
     */
    void on_init(const ServerConfig &configuration) override;

    /**
     * @brief Lifecycle hook binding the ORM query builder and scaffolding APIs
     * to the Lua state.
     *
     * Synchronizes on the Lua state mutex and calls @ref orm_engine::bind_lua
     * to register the global @c DB table.
     *
     * @param lua_engine_instance Reference to the initializing @ref lua_engine
     * instance.
     */
    void on_lua_init(lua_engine &lua_engine_instance) override;

    /**
     * @brief Lifecycle hook called during server shutdown to close active
     * database connections.
     *
     * Disconnects the active database driver gracefully before worker threads
     * terminate.
     */
    void on_server_shutdown() override;
};

} // namespace api
