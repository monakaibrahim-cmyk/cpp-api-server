#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace crow
{
struct request;
class response;
} // namespace crow

namespace api
{

class api_server;
class lua_engine;
class router;
struct ServerConfig;

/**
 * @brief Abstract base class representing any scriptable or modular extension
 * component.
 *
 * All specialized script hooks (@ref server_script, @ref request_script,
 * @ref lua_script, @ref handler_script, and @ref module_script) inherit from
 * this class. It provides human-readable identification and polymorphic cleanup
 * across the module management hierarchy.
 *
 * @headerfile api/script_mgr.h
 */
class script_object
{
  public:
    /**
     * @brief Constructs a new script object with an identification name.
     *
     * @param name Descriptive identifier for the script hook or extension
     * module.
     */
    explicit script_object(std::string name) : name_(std::move(name)) {}

    /**
     * @brief Virtual destructor ensuring proper polymorphic destruction of
     * derived hooks.
     */
    virtual ~script_object() = default;

    /**
     * @brief Retrieves the descriptive name of the script object.
     *
     * @return Const reference to the unique identification string.
     */
    const std::string &get_name() const { return name_; }

  private:
    std::string name_;
};

/**
 * @brief Script hook for intercepting server lifecycle and configuration
 * events.
 *
 * Subclasses override lifecycle callbacks to receive notifications when the
 * server starts listening, shuts down, or loads/reloads configuration files.
 * Subclasses are typically instantiated statically using the @ref
 * API_REGISTER_SCRIPT macro.
 *
 * @par Registration Example
 * @code{.cpp}
 * class custom_lifecycle_hook : public api::server_script
 * {
 * public:
 *     custom_lifecycle_hook()
 *         : api::server_script("custom_lifecycle_hook")
 *     {
 *     }
 *
 *     void on_server_startup(api::api_server& server_instance) override
 *     {
 *         // Initialize subsystems or external resources after listener is
 * online
 *     }
 *
 *     void on_server_shutdown() override
 *     {
 *         // Perform graceful cleanup before process termination
 *     }
 * };
 *
 * API_REGISTER_SCRIPT(custom_lifecycle_hook);
 * @endcode
 *
 * @thread_safety All virtual hook callbacks are dispatched under the @ref
 * script_mgr mutex lock. Hook implementations must avoid blocking operations or
 * recursive calls into @ref script_mgr.
 *
 * @headerfile api/script_mgr.h
 */
class server_script : public script_object
{
  public:
    /**
     * @brief Constructs a server lifecycle hook and automatically registers it
     * with @ref script_mgr.
     *
     * @param name Descriptive identifier for the lifecycle hook.
     */
    explicit server_script(std::string name);

    /**
     * @brief Virtual destructor for clean inheritance cleanup.
     */
    ~server_script() override = default;

    /**
     * @brief Invoked immediately after the HTTP server begins listening for
     * connections.
     *
     * @param server_instance Reference to the running @ref api_server instance.
     */
    virtual void on_server_startup(api_server &server_instance) {}

    /**
     * @brief Invoked when the server begins graceful shutdown before worker
     * threads terminate.
     */
    virtual void on_server_shutdown() {}

    /**
     * @brief Invoked during initial configuration loading from disk.
     *
     * @param configuration Mutable reference to the server configuration.
     */
    virtual void on_config_load(ServerConfig &configuration) {}

    /**
     * @brief Invoked when server configuration is dynamically reloaded at
     * runtime.
     *
     * @param configuration Mutable reference to the reloaded server
     * configuration.
     */
    virtual void on_config_reload(ServerConfig &configuration) {}
};

/**
 * @brief Script hook intercepting incoming and outgoing HTTP request
 * lifecycles.
 *
 * Allows pre-routing inspection, complete request short-circuiting, and
 * post-request metric or header collection across all HTTP traffic.
 *
 * @par Short-Circuit Example
 * @code{.cpp}
 * class maintenance_interceptor : public api::request_script
 * {
 * public:
 *     maintenance_interceptor()
 *         : api::request_script("maintenance_interceptor")
 *     {
 *     }
 *
 *     bool on_request_override(crow::request& request, crow::response&
 * response) override
 *     {
 *         if (in_maintenance_mode_)
 *         {
 *             response.code = 503;
 *             response.set_header("Content-Type", "application/json");
 *             response.write("{\"error\": \"Server is currently undergoing
 * scheduled maintenance\"}"); return true; // Halt further routing
 *         }
 *         return false; // Proceed normally
 *     }
 * private:
 *     bool in_maintenance_mode_ = false;
 * };
 *
 * API_REGISTER_SCRIPT(maintenance_interceptor);
 * @endcode
 *
 * @headerfile api/script_mgr.h
 */
class request_script : public script_object
{
  public:
    /**
     * @brief Constructs a request lifecycle hook and automatically registers it
     * with @ref script_mgr.
     *
     * @param name Descriptive identifier for the request hook.
     */
    explicit request_script(std::string name);

    /**
     * @brief Virtual destructor for polymorphic cleanup.
     */
    ~request_script() override = default;

    /**
     * @brief Invoked before router handling; allows early short-circuiting of
     * the request.
     *
     * If this method returns @c true, standard router processing is bypassed
     * and the populated @p response is transmitted immediately to the HTTP
     * client.
     *
     * @param request Incoming Crow HTTP request object.
     * @param response Outgoing Crow HTTP response object to populate if
     * overriding.
     * @return @c true if the request was fully handled and routing should
     * cease; @c false otherwise.
     */
    virtual bool on_request_override(crow::request &request,
                                     crow::response &response)
    {
        return false;
    }

    /**
     * @brief Invoked before an HTTP route handler processes the incoming
     * request.
     *
     * @param request Incoming Crow HTTP request object.
     * @param response Outgoing Crow HTTP response object.
     */
    virtual void on_before_request(crow::request &request,
                                   crow::response &response)
    {
    }

    /**
     * @brief Invoked after the HTTP route handler finishes and generated the
     * response.
     *
     * @param request Handled Crow HTTP request object.
     * @param response Outgoing Crow HTTP response object sent to the client.
     * @param duration_milliseconds Elapsed time in milliseconds consumed during
     * request processing.
     */
    virtual void on_after_request(crow::request &request,
                                  crow::response &response,
                                  double duration_milliseconds)
    {
    }
};

/**
 * @brief Script hook for registering custom C++ types and functions into the
 * Lua runtime.
 *
 * Enables extension modules to expose custom usertypes, enumerations, global
 * functions, and namespaces into Lua during bootstrap initialization.
 *
 * @par Registration Example
 * @code{.cpp}
 * class custom_lua_bindings : public api::lua_script
 * {
 * public:
 *     custom_lua_bindings()
 *         : api::lua_script("custom_lua_bindings")
 *     {
 *     }
 *
 *     void on_lua_init(api::lua_engine& lua_engine_instance) override
 *     {
 *         sol::state_view& lua_state = lua_engine_instance.state();
 *         lua_state.set_function("generate_uuid", &generate_uuid_string);
 *     }
 * };
 *
 * API_REGISTER_SCRIPT(custom_lua_bindings);
 * @endcode
 *
 * @headerfile api/script_mgr.h
 */
class lua_script : public script_object
{
  public:
    /**
     * @brief Constructs a Lua initialization script hook and registers it with
     * @ref script_mgr.
     *
     * @param name Descriptive identifier for the Lua script hook.
     */
    explicit lua_script(std::string name);

    /**
     * @brief Virtual destructor for clean polymorphic cleanup.
     */
    ~lua_script() override = default;

    /**
     * @brief Invoked during Lua engine bootstrap to register module usertypes
     * and global functions.
     *
     * @param lua_engine_instance Reference to the initializing @ref lua_engine
     * instance.
     */
    virtual void on_lua_init(lua_engine &lua_engine_instance) {}
};

/**
 * @brief Script hook for registering custom HTTP route endpoints directly in
 * C++.
 *
 * Allows C++ plugins and modules to define dedicated HTTP endpoints on the Crow
 * router alongside any Lua-defined script routes.
 *
 * @par Registration Example
 * @code{.cpp}
 * class system_routes : public api::handler_script
 * {
 * public:
 *     system_routes()
 *         : api::handler_script("system_routes")
 *     {
 *     }
 *
 *     void on_handlers_register(api::router& server_router) override
 *     {
 *         server_router.register_handler("GET", "/api/v1/system/status",
 *             [](const crow::request& request, crow::response& response)
 *             {
 *                 response.code = 200;
 *                 response.set_header("Content-Type", "application/json");
 *                 response.write("{\"status\": \"operational\"}");
 *             });
 *     }
 * };
 *
 * API_REGISTER_SCRIPT(system_routes);
 * @endcode
 *
 * @headerfile api/script_mgr.h
 */
class handler_script : public script_object
{
  public:
    /**
     * @brief Constructs an HTTP route handler script hook and registers it with
     * @ref script_mgr.
     *
     * @param name Descriptive identifier for the handler script hook.
     */
    explicit handler_script(std::string name);

    /**
     * @brief Virtual destructor for polymorphic cleanup.
     */
    ~handler_script() override = default;

    /**
     * @brief Invoked during server initialization to bind HTTP routes to the
     * server router.
     *
     * @param server_router Reference to the active @ref router instance.
     */
    virtual void on_handlers_register(router &server_router) {}
};

/**
 * @brief Comprehensive module base class bundling configuration, routing, and
 * Lua hooks.
 *
 * Represents a cohesive modular plugin combining configuration lifecycle
 * management, route handler definition, Lua usertype registration, and request
 * interception into a single structured component.
 *
 * @par Comprehensive Plugin Example
 * @code{.cpp}
 * class custom_analytics_module : public api::module_script
 * {
 * public:
 *     custom_analytics_module()
 *         : api::module_script("analytics", "1.2.0", "Embedded telemetry and
 * event ingestion module")
 *     {
 *     }
 *
 *     void on_init(const api::ServerConfig& configuration) override
 *     {
 *         // Read module-specific configuration sections
 *     }
 *
 *     void on_handlers_register(api::router& server_router) override
 *     {
 *         server_router.register_handler("POST", "/analytics/event",
 *             [this](const crow::request& request, crow::response& response)
 *             {
 *                 // Record event payload
 *                 response.code = 202;
 *             });
 *     }
 *
 *     void on_lua_init(api::lua_engine& lua_engine_instance) override
 *     {
 *         // Expose analytics logging function to Lua scripts
 *     }
 * };
 *
 * API_REGISTER_SCRIPT(custom_analytics_module);
 * @endcode
 *
 * @headerfile api/script_mgr.h
 */
class module_script : public script_object
{
  public:
    /**
     * @brief Constructs a module plugin instance and registers it with @ref
     * script_mgr.
     *
     * @param name Unique name identifying the module.
     * @param version Semantic version string (defaults to "1.0.0").
     * @param description Brief human-readable description of module
     * functionality.
     */
    explicit module_script(std::string name, std::string version = "1.0.0",
                           std::string description = "");

    /**
     * @brief Virtual destructor for polymorphic cleanup.
     */
    ~module_script() override = default;

    /**
     * @brief Retrieves the semantic version string of the module.
     *
     * @return Semantic version string (e.g., "1.0.0").
     */
    const std::string &get_version() const { return version_; }

    /**
     * @brief Retrieves the human-readable description of the module.
     *
     * @return Description string.
     */
    const std::string &get_description() const { return description_; }

    /**
     * @brief Invoked when server configuration is initially loaded from disk.
     *
     * @param configuration Mutable server configuration.
     */
    virtual void on_config_load(ServerConfig &configuration) {}

    /**
     * @brief Invoked during module initialization with read-only server
     * configuration.
     *
     * @param configuration Const reference to active server configuration.
     */
    virtual void on_init(const ServerConfig &configuration) {}

    /**
     * @brief Invoked when the HTTP server begins listening for connections.
     *
     * @param server_instance Reference to the running @ref api_server.
     */
    virtual void on_server_startup(api_server &server_instance) {}

    /**
     * @brief Invoked when the server begins graceful shutdown sequence.
     */
    virtual void on_server_shutdown() {}

    /**
     * @brief Invoked to register HTTP route endpoints provided by this module.
     *
     * @param server_router Reference to the active @ref router.
     */
    virtual void on_handlers_register(router &server_router) {}

    /**
     * @brief Invoked to bind Lua functions and usertypes provided by this
     * module.
     *
     * @param lua_engine_instance Reference to the active @ref lua_engine.
     */
    virtual void on_lua_init(lua_engine &lua_engine_instance) {}

    /**
     * @brief Invoked before request processing; allows overriding responses
     * early.
     *
     * @param request Incoming Crow HTTP request.
     * @param response Outgoing Crow HTTP response to populate if
     * short-circuiting.
     * @return @c true if request was handled and routing must stop; @c false
     * otherwise.
     */
    virtual bool on_request_override(crow::request &request,
                                     crow::response &response)
    {
        return false;
    }

    /**
     * @brief Invoked before route handler execution for incoming HTTP requests.
     *
     * @param request Incoming Crow HTTP request.
     * @param response Outgoing Crow HTTP response.
     */
    virtual void on_before_request(crow::request &request,
                                   crow::response &response)
    {
    }

    /**
     * @brief Invoked after route handler execution completes.
     *
     * @param request Completed Crow HTTP request.
     * @param response Outgoing Crow HTTP response sent to client.
     * @param duration_milliseconds Time taken to process request in
     * milliseconds.
     */
    virtual void on_after_request(crow::request &request,
                                  crow::response &response,
                                  double duration_milliseconds)
    {
    }

  private:
    std::string version_;
    std::string description_;
};

/**
 * @brief Central script and module manager dispatching lifecycle events across
 * hooks.
 *
 * Manages registered script hooks across five categories:
 * - Server lifecycle hooks (@ref server_script)
 * - Request interception hooks (@ref request_script)
 * - Lua binding hooks (@ref lua_script)
 * - Route registration hooks (@ref handler_script)
 * - Comprehensive module plugins (@ref module_script)
 *
 * Registration occurs automatically when hook instances are constructed
 * (typically via static variables instantiated through @ref
 * API_REGISTER_SCRIPT). When dispatching events, @ref script_mgr catches and
 * logs any exceptions thrown by individual hooks, isolating errors to prevent
 * server-wide disruption.
 *
 * @thread_safety All registration methods and event dispatchers are
 * synchronized via an internal @c std::mutex.
 *
 * @headerfile api/script_mgr.h
 */
class script_mgr
{
  public:
    /**
     * @brief Accesses the singleton instance of the script manager.
     *
     * @return Reference to the global @ref script_mgr singleton.
     */
    static script_mgr &instance();

    /**
     * @brief Logs the count of registered modules and script hooks.
     */
    void initialize();

    /**
     * @brief Registers a server lifecycle script hook.
     *
     * @param script_instance Pointer to the lifecycle hook to register.
     */
    void register_server_script(server_script *script_instance);

    /**
     * @brief Registers an HTTP request lifecycle script hook.
     *
     * @param script_instance Pointer to the request hook to register.
     */
    void register_request_script(request_script *script_instance);

    /**
     * @brief Registers a Lua engine binding script hook.
     *
     * @param script_instance Pointer to the Lua script hook to register.
     */
    void register_lua_script(lua_script *script_instance);

    /**
     * @brief Registers an HTTP route handler script hook.
     *
     * @param script_instance Pointer to the handler script hook to register.
     */
    void register_handler_script(handler_script *script_instance);

    /**
     * @brief Registers a comprehensive module plugin.
     *
     * @param script_instance Pointer to the module script to register.
     */
    void register_module_script(module_script *script_instance);

    /**
     * @brief Broadcasts configuration loading across all registered server and
     * module hooks.
     *
     * @param configuration Mutable server configuration loaded from disk.
     */
    void on_config_load(ServerConfig &configuration);

    /**
     * @brief Broadcasts configuration reload across all registered server
     * hooks.
     *
     * @param configuration Mutable server configuration reloaded dynamically.
     */
    void on_config_reload(ServerConfig &configuration);

    /**
     * @brief Broadcasts route registration across all registered handler and
     * module hooks.
     *
     * @param server_router Reference to the active @ref router instance.
     */
    void on_handlers_register(router &server_router);

    /**
     * @brief Broadcasts Lua initialization across all registered Lua and module
     * hooks.
     *
     * @param lua_engine_instance Reference to the initializing @ref lua_engine
     * instance.
     */
    void on_lua_init(lua_engine &lua_engine_instance);

    /**
     * @brief Dispatches request override hook; returns true if any script
     * intercepted it.
     *
     * @param request Incoming Crow HTTP request.
     * @param response Outgoing Crow HTTP response to populate if intercepted.
     * @return @c true if intercepted by any hook; @c false if routing should
     * proceed.
     */
    bool on_request_override(crow::request &request, crow::response &response);

    /**
     * @brief Broadcasts before-request notification to all registered request
     * and module hooks.
     *
     * @param request Incoming Crow HTTP request.
     * @param response Outgoing Crow HTTP response.
     */
    void on_before_request(crow::request &request, crow::response &response);

    /**
     * @brief Broadcasts after-request notification to all registered request
     * and module hooks.
     *
     * @param request Handled Crow HTTP request.
     * @param response Outgoing Crow HTTP response sent to client.
     * @param duration_milliseconds Total processing time in milliseconds.
     */
    void on_after_request(crow::request &request, crow::response &response,
                          double duration_milliseconds);

    /**
     * @brief Broadcasts server startup notification to all server and module
     * hooks.
     *
     * @param server_instance Reference to the running @ref api_server.
     */
    void on_server_startup(api_server &server_instance);

    /**
     * @brief Broadcasts server shutdown notification to all server and module
     * hooks.
     */
    void on_server_shutdown();

    /**
     * @brief Returns the total count of all registered scripts and modules.
     *
     * @return Total count across all five script hook collections.
     */
    size_t get_script_count() const;

    /**
     * @brief Returns a list of identification names for all registered scripts
     * and modules.
     *
     * @return Vector of script and module names.
     */
    std::vector<std::string> get_script_names() const;

    /**
     * @brief Returns pointers to all registered module plugins.
     *
     * @return Vector of @ref module_script pointers.
     */
    std::vector<module_script *> get_module_scripts() const;

  private:
    script_mgr() = default;
    mutable std::mutex mutex_;

    std::vector<server_script *> server_scripts_;
    std::vector<request_script *> request_scripts_;
    std::vector<lua_script *> lua_scripts_;
    std::vector<handler_script *> handler_scripts_;
    std::vector<module_script *> module_scripts_;
};

/**
 * @brief Helper function returning reference to global @ref script_mgr
 * singleton.
 *
 * @return Reference to the @ref script_mgr singleton instance.
 */
inline script_mgr &s_script_mgr() { return script_mgr::instance(); }

} // namespace api

/**
 * @def API_REGISTER_SCRIPT_IMPL(script_class, counter)
 * @brief Internal implementation macro creating a static registrar struct
 * instance.
 * @internal
 */
#define API_REGISTER_SCRIPT_IMPL(script_class, counter)                        \
    namespace                                                                  \
    {                                                                          \
    struct ScriptRegistrar_##counter                                           \
    {                                                                          \
        ScriptRegistrar_##counter() { static script_class static_instance; }   \
    };                                                                         \
    static const ScriptRegistrar_##counter static_script_registrar_##counter;  \
    }

/**
 * @def API_REGISTER_SCRIPT_EXPAND(script_class, counter)
 * @brief Macro expansion intermediary ensuring @c __COUNTER__ is expanded
 * before token pasting.
 * @internal
 */
#define API_REGISTER_SCRIPT_EXPAND(script_class, counter)                      \
    API_REGISTER_SCRIPT_IMPL(script_class, counter)

/**
 * @def API_REGISTER_SCRIPT(script_class)
 * @brief Macro registering a script hook class statically at program startup.
 *
 * Declares an anonymous namespace containing a static registrar struct whose
 * constructor forces the static instantiation of @p script_class before @c
 * main() commences. The base class constructor automatically registers the
 * instance with @ref api::script_mgr.
 *
 * @par Usage Example
 * @code{.cpp}
 * class my_custom_hook : public api::server_script
 * {
 * public:
 *     my_custom_hook() : api::server_script("my_custom_hook") {}
 * };
 *
 * API_REGISTER_SCRIPT(my_custom_hook);
 * @endcode
 *
 * @param script_class Derived class type inheriting from one of the script hook
 * base classes.
 */
#define API_REGISTER_SCRIPT(script_class)                                      \
    API_REGISTER_SCRIPT_EXPAND(script_class, __COUNTER__)
