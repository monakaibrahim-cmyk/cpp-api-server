#pragma once

#include <core/module.h>

#include <string>

namespace api
{

struct ServerConfig;
class router;
class lua_engine;
class api_server;

/**
 * @brief Example developer-defined data transfer struct exposed to Lua
 * scripting.
 *
 * Demonstrates how C++ Plain Old Data (POD) structs are exposed to the Sol2 Lua
 * engine with direct member variable access.
 *
 * @par Lua Usage Example
 * @code{.lua}
 * local item = mod_template.template_data.new()
 * item.id = "txn_90210"
 * item.value = "Processed order batch"
 * item.priority = 10
 *
 * print("Created item: " .. item.id .. " with priority " ..
 * tostring(item.priority))
 * @endcode
 *
 * @headerfile mod_template.h
 */
struct TemplateData
{
    /**
     * @brief Unique record identifier string.
     */
    std::string id;

    /**
     * @brief Arbitrary text value payload.
     */
    std::string value;

    /**
     * @brief Processing priority rank (higher numerical value represents higher
     * priority).
     */
    int priority = 0;
};

/**
 * @brief Backward compatibility alias for @ref TemplateData conforming to
 * legacy snake_case.
 */
using template_data = TemplateData;

/**
 * @brief Example developer-defined C++ service class with methods callable from
 * Lua.
 *
 * Demonstrates how C++ classes with member methods and properties are bound to
 * Sol2 usertypes. Can be consumed directly within native C++ modules or
 * dynamically invoked within Lua HTTP route handlers.
 *
 * @par C++ Usage Example
 * @code{.cpp}
 * api::template_service service("batch_processor");
 * api::TemplateData record{"REC-001", "Payload data", 5};
 *
 * if (service.process_item(record))
 * {
 *     int total = service.calculate(10, 25); // returns 35
 * }
 * @endcode
 *
 * @par Lua Usage Example
 * @code{.lua}
 * -- Using the module's pre-instantiated singleton service:
 * local service = mod_template.active_service
 * service.name = "reconfigured_processor"
 * local sum = service:calculate(15, 30) -- returns 45
 *
 * -- Instantiating a new service instance in Lua:
 * local custom_service = mod_template.template_service.new("custom_worker")
 * print("Worker name: " .. custom_service.name)
 * @endcode
 *
 * @headerfile mod_template.h
 */
class template_service
{
  public:
    /**
     * @brief Constructs a @ref template_service with the given service
     * identification name.
     *
     * @param service_name Name identifying this service instance (defaults to
     * "default").
     */
    explicit template_service(std::string service_name = "default");

    /**
     * @brief Default destructor.
     */
    ~template_service() = default;

    /**
     * @brief Retrieves the service identification name.
     *
     * @return Const reference to the service name string.
     */
    const std::string &name() const;

    /**
     * @brief Updates the service identification name.
     *
     * @param service_name New service name string.
     */
    void set_name(const std::string &service_name);

    /**
     * @brief Performs integer arithmetic calculation.
     *
     * @param first_number First integer operand.
     * @param second_number Second integer operand.
     * @return Sum of the two operands.
     */
    int calculate(int first_number, int second_number) const;

    /**
     * @brief Validates and processes a @ref TemplateData struct instance.
     *
     * @param item Const reference to the @ref TemplateData record to process.
     * @return @c true if the item was valid and processed; @c false otherwise.
     */
    bool process_item(const TemplateData &item);

  private:
    std::string name_;
};

/**
 * @brief Template module demonstrating registration of custom classes, structs,
 * and Lua bindings.
 *
 * Serves as an architectural template and reference implementation for
 * third-party developer modules. Demonstrates:
 * - Subclassing @ref module and registering via @ref API_REGISTER_MODULE.
 * - Exposing C++ structs (@ref TemplateData) and classes (@ref
 * template_service) to Lua via Sol2.
 * - Exposing global variables, functions, and module namespaces to Lua.
 * - Implementing lifecycle event hooks for configuration, routing, and request
 * interceptors.
 *
 * @par Lua Environment Exposed by mod_template
 * @code{.lua}
 * -- Namespace: mod_template
 * print("Module version: " .. mod_template.version)
 *
 * -- Standalone namespace function:
 * local product = mod_template.quick_calc(6, 7) -- returns 42
 *
 * -- Struct creation and manipulation:
 * local data = mod_template.template_data.new()
 * data.id = "ITEM-100"
 * data.priority = 1
 *
 * -- Class method invocation:
 * local status = mod_template.active_service:process_item(data)
 * @endcode
 *
 * @headerfile mod_template.h
 */
class mod_template : public module
{
  public:
    /**
     * @brief Constructs the @ref mod_template module descriptor with metadata.
     */
    mod_template();

    /**
     * @brief Virtual destructor for clean module teardown.
     */
    ~mod_template() override = default;

    /**
     * @brief Lifecycle hook called during server configuration loading.
     *
     * @param configuration Mutable server configuration loaded from disk.
     */
    void on_config_load(ServerConfig &configuration) override;

    /**
     * @brief Lifecycle hook called during module initialization.
     *
     * @param configuration Const reference to the active server configuration.
     */
    void on_init(const ServerConfig &configuration) override;

    /**
     * @brief Lifecycle hook called to register custom C++ HTTP route endpoints.
     *
     * @param server_router Reference to the active @ref router instance.
     */
    void on_handlers_register(router &server_router) override;

    /**
     * @brief Lifecycle hook binding C++ classes, structs, variables, and
     * functions into Lua.
     *
     * Creates Sol2 usertypes for @ref TemplateData and @ref template_service,
     * and registers them along with module variables and helper lambdas under
     * the @c mod_template Lua table.
     *
     * @param lua_engine_instance Reference to the initializing @ref lua_engine
     * instance.
     */
    void on_lua_init(lua_engine &lua_engine_instance) override;

    /**
     * @brief Lifecycle hook allowing pre-routing request interception and
     * short-circuiting.
     *
     * @param request Incoming Crow HTTP request.
     * @param response Outgoing Crow HTTP response to populate if
     * short-circuiting.
     * @return @c true if request was handled and routing must halt; @c false to
     * continue routing.
     */
    bool on_request_override(crow::request &request,
                             crow::response &response) override;

    /**
     * @brief Lifecycle hook executed before each incoming HTTP request is
     * dispatched.
     *
     * @param request Incoming Crow HTTP request.
     * @param response Outgoing Crow HTTP response.
     */
    void on_before_request(crow::request &request,
                           crow::response &response) override;

    /**
     * @brief Lifecycle hook executed after each HTTP request completes.
     *
     * @param request Completed Crow HTTP request.
     * @param response Outgoing Crow HTTP response sent to client.
     * @param duration_milliseconds Execution duration in milliseconds.
     */
    void on_after_request(crow::request &request, crow::response &response,
                          double duration_milliseconds) override;

    /**
     * @brief Lifecycle hook executed when the HTTP server starts listening for
     * connections.
     *
     * @param server_instance Reference to the active @ref api_server instance.
     */
    void on_server_startup(api_server &server_instance) override;

    /**
     * @brief Lifecycle hook executed when the server begins its shutdown
     * sequence.
     */
    void on_server_shutdown() override;

  private:
    template_service default_service_{"primary_service"};
};

} // namespace api
