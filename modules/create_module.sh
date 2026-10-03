#!/usr/bin/env bash
# Helper script to generate a new modular C++ module
# Usage: ./modules/create_module.sh <module_name>

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <module_name>"
    echo "Example: $0 mod_database"
    exit 1
fi

MOD_NAME="$1"
PASCAL_NAME=$(echo "$MOD_NAME" | sed -r 's/(^|_)([a-z])/\U\2/g')
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="$SCRIPT_DIR/$MOD_NAME"

if [ -d "$TARGET_DIR" ]; then
    echo "Error: Module directory '$TARGET_DIR' already exists!"
    exit 1
fi

echo "Creating module '$MOD_NAME'..."
mkdir -p "$TARGET_DIR/include" "$TARGET_DIR/src" "$TARGET_DIR/conf"

cat > "$TARGET_DIR/CMakeLists.txt" << EOF
cmake_minimum_required(VERSION 3.25)

set(MOD_NAME ${MOD_NAME})

file(GLOB_RECURSE MOD_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

add_library(\${MOD_NAME} STATIC
    \${MOD_SOURCES}
)

target_include_directories(\${MOD_NAME} PUBLIC
    include
    \${CMAKE_SOURCE_DIR}/include
    \${sol2_SOURCE_DIR}/include
)

target_link_libraries(\${MOD_NAME} PUBLIC
    Crow::Crow
    lua_lib
)
EOF

cat > "$TARGET_DIR/conf/${MOD_NAME}.lua.dist" << EOF
${MOD_NAME} = {
    enabled = true,
}
EOF

cat > "$TARGET_DIR/include/${MOD_NAME}.h" << EOF
#pragma once

#include "api/module.h"

#include <string>

namespace api
{

struct ServerConfig;
class router;
class lua_engine;
class api_server;

/**
 * @brief Developer-defined data structure exposed to Lua scripting.
 */
struct ${PASCAL_NAME}Data
{
    /** @brief Unique key identifier string. */
    std::string key;

    /** @brief Associated text value payload. */
    std::string value;

    /** @brief Numerical execution or priority rank. */
    int priority = 0;
};

/** Backward compatibility alias for ${PASCAL_NAME}Data. */
using ${MOD_NAME}_data = ${PASCAL_NAME}Data;

/**
 * @brief Developer-defined service class with methods callable from Lua and C++.
 */
class ${PASCAL_NAME}Service
{
public:
    /**
     * @brief Constructs service with specified identifier name.
     *
     * @param service_name Name identifying this service instance.
     */
    explicit ${PASCAL_NAME}Service(std::string service_name = "default");

    /**
     * @brief Destructor.
     */
    ~${PASCAL_NAME}Service() = default;

    /**
     * @brief Retrieves service identifier name.
     *
     * @return Service name string.
     */
    const std::string& name() const;

    /**
     * @brief Updates service identifier name.
     *
     * @param service_name New service name string.
     */
    void set_name(const std::string& service_name);

    /**
     * @brief Performs integer arithmetic calculation.
     *
     * @param first_number First integer operand.
     * @param second_number Second integer operand.
     * @return Sum of the operands.
     */
    int calculate(int first_number, int second_number) const;

private:
    std::string name_;
};

/**
 * @brief Custom extension module plugin.
 */
class ${MOD_NAME} : public module
{
public:
    /**
     * @brief Constructs module descriptor.
     */
    ${MOD_NAME}();

    /**
     * @brief Virtual destructor.
     */
    ~${MOD_NAME}() override = default;

    /** @brief Lifecycle hook called during configuration load. */
    void on_config_load(ServerConfig& configuration) override;

    /** @brief Lifecycle hook called during module initialization. */
    void on_init(const ServerConfig& configuration) override;

    /** @brief Lifecycle hook called to register custom route endpoints. */
    void on_handlers_register(router& server_router) override;

    /** @brief Lifecycle hook called to register Lua bindings. */
    void on_lua_init(lua_engine& lua_engine_instance) override;

    /** @brief Lifecycle hook allowing request interception and override. */
    bool on_request_override(crow::request& request, crow::response& response) override;

    /** @brief Lifecycle hook executed before each request is handled. */
    void on_before_request(crow::request& request, crow::response& response) override;

    /** @brief Lifecycle hook executed after each request finishes. */
    void on_after_request(crow::request& request, crow::response& response, double duration_milliseconds) override;

    /** @brief Lifecycle hook executed when server finishes startup. */
    void on_server_startup(api_server& server_instance) override;

    /** @brief Lifecycle hook executed when server begins shutdown. */
    void on_server_shutdown() override;

private:
    ${PASCAL_NAME}Service default_service_{"primary"};
};

} // namespace api
EOF

cat > "$TARGET_DIR/src/${MOD_NAME}.cpp" << EOF
#include "${MOD_NAME}.h"

#include "api/config.h"
#include "api/logger.h"
#include "api/lua_binding.h"
#include "api/lua_engine.h"
#include "api/module_registry.h"
#include "api/router.h"
#include "api/service_registry.h"

#include <crow.h>

namespace api
{

${PASCAL_NAME}Service::${PASCAL_NAME}Service(std::string service_name)
    : name_(std::move(service_name))
{
}

const std::string& ${PASCAL_NAME}Service::name() const
{
    return name_;
}

void ${PASCAL_NAME}Service::set_name(const std::string& service_name)
{
    name_ = service_name;
}

int ${PASCAL_NAME}Service::calculate(int first_number, int second_number) const
{
    return first_number + second_number;
}

${MOD_NAME}::${MOD_NAME}()
    : module("${MOD_NAME}", "1.0.0", "Extension module")
{
}

void ${MOD_NAME}::on_config_load(ServerConfig& /*configuration*/)
{
}

void ${MOD_NAME}::on_init(const ServerConfig& /*configuration*/)
{
    LOG_INFO("${MOD_NAME}", "Initialized");
}

void ${MOD_NAME}::on_handlers_register(router& /*server_router*/)
{
}

void ${MOD_NAME}::on_lua_init(lua_engine& lua_engine_instance)
{
    std::lock_guard<std::mutex> lock(lua_engine_instance.mutex());
    auto& lua_state = lua_engine_instance.state();

    // ── 1. Struct UserType Binding ─────────────────────────────────
    lua_state.new_usertype<${PASCAL_NAME}Data>(
        "${MOD_NAME}_data",
        sol::constructors<${PASCAL_NAME}Data()>(),
        "key", &${PASCAL_NAME}Data::key,
        "value", &${PASCAL_NAME}Data::value,
        "priority", &${PASCAL_NAME}Data::priority
    );

    // ── 2. Class UserType Binding ──────────────────────────────────
    lua_state.new_usertype<${PASCAL_NAME}Service>(
        "${MOD_NAME}_service",
        sol::constructors<${PASCAL_NAME}Service(), ${PASCAL_NAME}Service(std::string)>(),
        "name", sol::property(&${PASCAL_NAME}Service::name, &${PASCAL_NAME}Service::set_name),
        "calculate", &${PASCAL_NAME}Service::calculate
    );

    // ── 3. Namespace Binding ───────────────────────────────────────
    sol::table module_namespace = lua_get_or_create_namespace(lua_state, "${MOD_NAME}");

    module_namespace["${MOD_NAME}_data"] = lua_state["${MOD_NAME}_data"];
    module_namespace["${MOD_NAME}_service"] = lua_state["${MOD_NAME}_service"];

    // ── 4. Variable Binding ────────────────────────────────────────
    module_namespace["version"] = "1.0.0";
    module_namespace["service"] = &default_service_;

    // ── 5. Function Binding ────────────────────────────────────────
    module_namespace["help"] = []() -> std::string
    {
        return "${MOD_NAME} module v1.0.0";
    };

    LOG_INFO("${MOD_NAME}", "Registered Lua bindings: struct, class, namespace, variables, and functions");
}

bool ${MOD_NAME}::on_request_override(
    crow::request& /*request*/,
    crow::response& /*response*/
)
{
    return false;
}

void ${MOD_NAME}::on_before_request(
    crow::request& /*request*/,
    crow::response& /*response*/
)
{
}

void ${MOD_NAME}::on_after_request(
    crow::request& /*request*/,
    crow::response& /*response*/,
    double /*duration_milliseconds*/
)
{
}

void ${MOD_NAME}::on_server_startup(api_server& /*server_instance*/)
{
}

void ${MOD_NAME}::on_server_shutdown()
{
}

} // namespace api

API_REGISTER_MODULE(api::${MOD_NAME})
EOF

chmod +x "$0"
echo "Module '$MOD_NAME' created at modules/$MOD_NAME"
