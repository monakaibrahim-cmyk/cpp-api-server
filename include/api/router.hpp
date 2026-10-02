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

using native_handler_t = std::function<crow::response(const crow::request&)>;

class router
{
public:
    router(api_server& server, lua_engine& lua);

    void register_native_handler(const std::string& name, native_handler_t handler);
    void override_native_handler(const std::string& name, native_handler_t handler);
    bool has_native_handler(const std::string& name) const;

    void register_handler(const std::string& name, native_handler_t handler);
    void override_handler(const std::string& name, native_handler_t handler);
    bool has_handler(const std::string& name) const;

    void load_routes(const std::string& script_path);
    void load_routes_from_dir(const std::string& dir_path);

    size_t route_count() const;
    size_t handler_count() const;

private:
    void setup_lua_route_binding();
    crow::HTTPMethod parse_method(const std::string& method_str);

    api_server& server_;
    lua_engine& lua_;
    size_t route_count_ = 0;
    bool binding_registered_ = false;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, native_handler_t> native_handlers_;
    std::vector<sol::protected_function> lua_handlers_;
};

} // namespace api
