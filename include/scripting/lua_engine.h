#pragma once

#include <chrono>
#include <mutex>
#include <string>

#include <sol/sol.hpp>

namespace crow
{
namespace json
{
class rvalue;
}
} // namespace crow

namespace api
{

class metrics_collector;
class connection_tracker;
struct ServerConfig;

/**
 * @brief Thread-safe Lua 5.4 script engine and C++ interoperability bridge.
 *
 * @details Embeds a high-performance Lua 5.4 runtime using Sol2. Exposes core
 * server capabilities (telemetry, query builder, response cache, thread pool,
 * logging) to Lua scripts, and coordinates route handler callbacks.
 *
 * @note Concurrency Model:
 * Sol2 and the underlying Lua C API state are not internally reentrant or
 * thread-safe. To ensure safe execution across multiple HTTP worker threads,
 * any operation touching
 * @ref state MUST acquire the lock returned by @ref mutex():
 * @code{.cpp}
 * {
 *     std::lock_guard<std::mutex> lock(lua_engine_instance.mutex());
 *     sol::state& state = lua_engine_instance.state();
 *     state.script("print('Executing under mutex lock')");
 * }
 * @endcode
 *
 * Standard libraries initialized at bootstrap:
 * - @c base, @c string, @c table, @c math, @c io, @c os, @c package
 */
class lua_engine
{
  public:
    /**
     * @brief Initializes the Lua runtime, opens standard libraries, and
     * registers core functions.
     */
    lua_engine();

    /**
     * @brief Destructor releasing the Sol2 state and runtime allocations.
     */
    ~lua_engine();

    /**
     * @brief Binds core C++ subsystems, telemetry tables, and usertypes into
     * the Lua environment.
     *
     * @details Exposes the following global tables and types to Lua scripts:
     * - @c api : Uptime, formatting helpers, version, thread pool, cache
     * access.
     * - @c log : log.info, log.warn, log.error, log.debug.
     * - @c modules : Module discovery and lifecycle inspection.
     * - @c server_config, @c connection_stats, @c system_snapshot, @c
     * cached_response usertypes.
     *
     * @param[in] metrics_collector_instance Pointer to system metrics collector
     * (optional).
     * @param[in] connection_tracker_instance Pointer to active connection
     * tracker (optional).
     * @param[in] server_configuration Pointer to server configuration structure
     * (optional).
     */
    void bind_core_api(metrics_collector *metrics_collector_instance,
                       connection_tracker *connection_tracker_instance,
                       const ServerConfig *server_configuration = nullptr);

    /**
     * @brief Configures Lua package.path search paths across script and module
     * directories.
     *
     * @details Recursively scans @p base_directory and automatically appends
     * subdirectories, allowing nested Lua files to be loaded seamlessly via
     * standard @c require calls.
     *
     * @param[in] base_directory Root directory for route scripts and shared
     * modules.
     */
    void setup_package_path(const std::string &base_directory);

    /**
     * @brief Loads and executes a Lua script file from the filesystem.
     *
     * @details Thread-safe wrapper acquiring @ref mutex. Catches and logs
     * syntax errors or runtime script exceptions without crashing the host
     * process.
     *
     * @param[in] script_path Filesystem path to the .lua script file.
     */
    void load_file(const std::string &script_path);

    /**
     * @brief Converts a Crow JSON rvalue into an equivalent Sol2 Lua object.
     *
     * @param[in,out] state Active Sol2 state view.
     * @param[in] value Crow JSON rvalue to transform.
     * @return sol::object Representing the converted Lua primitive or table.
     */
    static sol::object json_to_lua(sol::state_view state,
                                   const crow::json::rvalue &value);

    /**
     * @brief Serializes a Sol2 Lua object or table into a valid JSON string.
     *
     * @param[in] value Lua object to serialize.
     * @return std::string JSON representation.
     */
    static std::string lua_to_json(const sol::object &value);

    /**
     * @brief Accesses the underlying Sol2 Lua state.
     *
     * @warning The caller MUST hold the @ref mutex lock while interacting with
     * the returned state.
     *
     * @return sol::state& Reference to active Sol2 state.
     */
    sol::state &state();

    /**
     * @brief Returns the mutual exclusion lock protecting the Lua runtime
     * state.
     *
     * @return std::mutex& Reference to synchronization mutex.
     */
    std::mutex &mutex();

  private:
    sol::state lua_;
    std::mutex mutex_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace api
