#!/usr/bin/env bash
# Helper script to generate a new AzerothCore-style module
# Usage: ./modules/create_module.sh <module_name>

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <module_name>"
    echo "Example: $0 mod_database"
    exit 1
fi

MOD_NAME="$1"
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

cat > "$TARGET_DIR/include/${MOD_NAME}.hpp" << EOF
#pragma once

#include "api/module.hpp"

namespace api
{

class ${MOD_NAME} : public module
{
public:
    ${MOD_NAME}();
    ~${MOD_NAME}() override = default;

    void on_config_load(server_config& cfg) override;
    void on_init(const server_config& cfg) override;
    void on_handlers_register(router& rtr) override;
    void on_lua_init(lua_engine& lua) override;
    bool on_request_override(crow::request& req, crow::response& res) override;
    void on_before_request(crow::request& req, crow::response& res) override;
    void on_after_request(crow::request& req, crow::response& res, double duration_ms) override;
    void on_server_startup(api_server& server) override;
    void on_server_shutdown() override;
};

} // namespace api
EOF

cat > "$TARGET_DIR/src/${MOD_NAME}.cpp" << EOF
#include "${MOD_NAME}.hpp"
#include "api/module_registry.hpp"
#include "api/logger.hpp"
#include "api/lua_engine.hpp"
#include "api/router.hpp"
#include "api/service_registry.hpp"

#include <crow.h>

namespace api
{

${MOD_NAME}::${MOD_NAME}()
    : module("${MOD_NAME}", "1.0.0", "Extension module")
{
}

void ${MOD_NAME}::on_config_load(server_config& /*cfg*/)
{
}

void ${MOD_NAME}::on_init(const server_config& /*cfg*/)
{
    LOG_INFO("${MOD_NAME}", "Initialized");
}

void ${MOD_NAME}::on_handlers_register(router& /*rtr*/)
{
}

void ${MOD_NAME}::on_lua_init(lua_engine& lua)
{
    std::lock_guard<std::mutex> lock(lua.mutex());
}

bool ${MOD_NAME}::on_request_override(crow::request& /*req*/, crow::response& /*res*/)
{
    return false;
}

void ${MOD_NAME}::on_before_request(crow::request& /*req*/, crow::response& /*res*/)
{
}

void ${MOD_NAME}::on_after_request(crow::request& /*req*/, crow::response& /*res*/, double /*duration_ms*/)
{
}

void ${MOD_NAME}::on_server_startup(api_server& /*server*/)
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
