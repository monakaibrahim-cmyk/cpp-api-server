#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <crow.h>
#include <sol/sol.hpp>

namespace api
{

class api_server;
class lua_engine;

/**
 * @brief Function signature callback for native C++ HTTP request handlers.
 */
using native_handler_type =
    std::function<crow::response(const crow::request &)>;

/// Backward compatibility alias
using native_handler_t = native_handler_type;

/**
 * @brief Function signature callback for parameterized REST native C++
 * handlers.
 */
using rest_native_handler_type = std::function<crow::response(
    const crow::request &,
    const std::unordered_map<std::string, std::string> &)>;

/**
 * @brief Representation of a registered RESTful route pattern supporting path
 * parameters.
 */
struct RestRoutePattern
{
    std::string raw_path;
    crow::HTTPMethod method = crow::HTTPMethod::GET;
    std::vector<std::string> path_segments;
    std::vector<std::string> parameter_names;
    bool has_parameters = false;
    int cache_time_to_live_seconds = 0;
    std::string native_handler_name;
    size_t lua_handler_index = static_cast<size_t>(-1);
};

/**
 * @brief Dynamic HTTP request router bridging Crow endpoints with C++ handlers
 * and Lua scripts.
 *
 * @details Exposes a global @c route function in the Lua environment, enabling
 * route definitions to bind either to compiled native C++ handlers or Lua
 * lambda functions. Every incoming request processed by the router is
 * automatically passed through:
 * 1. Apache @ref htaccess_engine policy evaluation (rewrites, redirects, access
 * control).
 * 2. In-memory response caching (@ref cache_engine) with configurable per-route
 * TTL.
 *
 * Example C++ handler registration:
 * @code{.cpp}
 * server_router.register_native_handler("users.list", [](const crow::request&
 * req) { return crow::response(200, "{\"users\":[]}");
 * });
 * @endcode
 *
 * Example Lua route mappings (@c scripts/routes.lua):
 * @code{.lua}
 * -- Map to native C++ handler with 60-second caching:
 * route("GET", "/api/v1/users", "users.list", { cache = 60 })
 *
 * -- Direct Lua route handler:
 * route("POST", "/api/v1/echo", function(req)
 *     return { status = 200, body = req.body, content_type = "application/json"
 * } end)
 * @endcode
 */
class router
{
  public:
    /**
     * @brief Constructs router bound to the parent server and Lua runtime.
     *
     * @param[in,out] server_instance Reference to owning api_server.
     * @param[in,out] lua_engine_instance Reference to Lua engine.
     */
    router(api_server &server_instance, lua_engine &lua_engine_instance);

    /**
     * @brief Registers a named native C++ handler callback.
     *
     * @param[in] handler_name Unique identifier string (e.g. "auth.login").
     * @param[in] native_handler Invocable callback matching @ref
     * native_handler_type.
     */
    void register_native_handler(const std::string &handler_name,
                                 native_handler_type native_handler);

    /**
     * @brief Overrides an existing named native C++ handler callback.
     *
     * @param[in] handler_name Unique identifier string.
     * @param[in] native_handler Replacement callback.
     */
    void override_native_handler(const std::string &handler_name,
                                 native_handler_type native_handler);

    /**
     * @brief Tests if a native handler is registered under the given name.
     *
     * @param[in] handler_name Unique identifier string.
     * @return true If registered; false otherwise.
     */
    bool has_native_handler(const std::string &handler_name) const;

    /**
     * @brief Convenience alias for @ref register_native_handler.
     */
    void register_handler(const std::string &handler_name,
                          native_handler_type native_handler);

    /**
     * @brief Convenience alias for @ref override_native_handler.
     */
    void override_handler(const std::string &handler_name,
                          native_handler_type native_handler);

    /**
     * @brief Convenience alias for @ref has_native_handler.
     */
    bool has_handler(const std::string &handler_name) const;

    /**
     * @brief Registers a parameterized REST native C++ handler callback.
     *
     * @param[in] handler_name Unique identifier string (e.g.
     * "users.get_by_id").
     * @param[in] rest_handler Invocable callback matching @ref
     * rest_native_handler_type.
     */
    void register_rest_native_handler(const std::string &handler_name,
                                      rest_native_handler_type rest_handler);

    /**
     * @brief Convenience alias for @ref register_rest_native_handler.
     */
    void register_rest_handler(const std::string &handler_name,
                               rest_native_handler_type rest_handler);

    /**
     * @brief Dispatches an incoming HTTP request across registered REST route
     * patterns.
     *
     * @param[in] request Active Crow HTTP request.
     * @return crow::response Evaluated HTTP response.
     */
    crow::response dispatch_request(const crow::request &request);

    /**
     * @brief Loads and executes an individual Lua route script file.
     *
     * @param[in] route_script_path Path to target .lua script file.
     */
    void load_routes(const std::string &route_script_path);

    /**
     * @brief Recursively loads all .lua route scripts from the specified
     * directory.
     *
     * @param[in] directory_path Filesystem directory containing route files.
     */
    void load_routes_from_dir(const std::string &directory_path);

    /**
     * @brief Returns the total number of dynamic routes registered across all
     * scripts.
     *
     * @return size_t Count of active endpoints.
     */
    size_t route_count() const;

    /**
     * @brief Returns the count of registered native C++ handler callbacks.
     *
     * @return size_t Count of native handlers.
     */
    size_t handler_count() const;

  private:
    void setup_lua_route_binding();
    void bind_dynamic_crow_route(const std::string &path,
                                 crow::HTTPMethod method);
    crow::HTTPMethod parse_method(const std::string &method_string);
    static std::vector<std::string> split_path(const std::string &path_string);
    static bool match_route(
        const RestRoutePattern &route_pattern,
        const std::vector<std::string> &request_segments,
        std::unordered_map<std::string, std::string> &extracted_parameters);

    api_server &server_;
    lua_engine &lua_;
    size_t route_count_ = 0;
    bool binding_registered_ = false;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, native_handler_type> native_handlers_;
    std::unordered_map<std::string, rest_native_handler_type>
        rest_native_handlers_;
    std::vector<sol::protected_function> lua_handlers_;
    std::vector<RestRoutePattern> rest_routes_;
};

} // namespace api
