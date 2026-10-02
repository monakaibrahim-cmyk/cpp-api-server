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
struct server_config;

class script_object
{
public:
    explicit script_object(std::string name)
        : name_(std::move(name))
    {
    }

    virtual ~script_object() = default;

    const std::string& get_name() const
    {
        return name_;
    }

private:
    std::string name_;
};

class server_script : public script_object
{
public:
    explicit server_script(std::string name);
    ~server_script() override = default;

    virtual void on_server_startup(api_server& /*server*/)
    {
    }

    virtual void on_server_shutdown()
    {
    }

    virtual void on_config_load(server_config& /*cfg*/)
    {
    }

    virtual void on_config_reload(server_config& /*cfg*/)
    {
    }
};

class request_script : public script_object
{
public:
    explicit request_script(std::string name);
    ~request_script() override = default;

    virtual bool on_request_override(
        crow::request& /*req*/,
        crow::response& /*res*/)
    {
        return false;
    }

    virtual void on_before_request(
        crow::request& /*req*/,
        crow::response& /*res*/)
    {
    }

    virtual void on_after_request(
        crow::request& /*req*/,
        crow::response& /*res*/,
        double /*duration_ms*/)
    {
    }
};

class lua_script : public script_object
{
public:
    explicit lua_script(std::string name);
    ~lua_script() override = default;

    virtual void on_lua_init(lua_engine& /*lua*/)
    {
    }
};

class handler_script : public script_object
{
public:
    explicit handler_script(std::string name);
    ~handler_script() override = default;

    virtual void on_handlers_register(router& /*rtr*/)
    {
    }
};

class module_script : public script_object
{
public:
    explicit module_script(
        std::string name,
        std::string version = "1.0.0",
        std::string description = "");
    ~module_script() override = default;

    const std::string& get_version() const
    {
        return version_;
    }

    const std::string& get_description() const
    {
        return description_;
    }

    virtual void on_config_load(server_config& /*cfg*/)
    {
    }

    virtual void on_init(const server_config& /*cfg*/)
    {
    }

    virtual void on_server_startup(api_server& /*server*/)
    {
    }

    virtual void on_server_shutdown()
    {
    }

    virtual void on_handlers_register(router& /*rtr*/)
    {
    }

    virtual void on_lua_init(lua_engine& /*lua*/)
    {
    }

    virtual bool on_request_override(
        crow::request& /*req*/,
        crow::response& /*res*/)
    {
        return false;
    }

    virtual void on_before_request(
        crow::request& /*req*/,
        crow::response& /*res*/)
    {
    }

    virtual void on_after_request(
        crow::request& /*req*/,
        crow::response& /*res*/,
        double /*duration_ms*/)
    {
    }

private:
    std::string version_;
    std::string description_;
};

class script_mgr
{
public:
    static script_mgr& instance();

    void initialize();

    void register_server_script(server_script* script);
    void register_request_script(request_script* script);
    void register_lua_script(lua_script* script);
    void register_handler_script(handler_script* script);
    void register_module_script(module_script* script);

    void on_config_load(server_config& cfg);
    void on_config_reload(server_config& cfg);
    void on_handlers_register(router& rtr);
    void on_lua_init(lua_engine& lua);
    bool on_request_override(crow::request& req, crow::response& res);
    void on_before_request(crow::request& req, crow::response& res);
    void on_after_request(crow::request& req, crow::response& res, double duration_ms);
    void on_server_startup(api_server& server);
    void on_server_shutdown();

    size_t get_script_count() const;
    std::vector<std::string> get_script_names() const;
    std::vector<module_script*> get_module_scripts() const;

private:
    script_mgr() = default;
    mutable std::mutex mutex_;

    std::vector<server_script*> server_scripts_;
    std::vector<request_script*> request_scripts_;
    std::vector<lua_script*> lua_scripts_;
    std::vector<handler_script*> handler_scripts_;
    std::vector<module_script*> module_scripts_;
};

inline script_mgr& s_script_mgr()
{
    return script_mgr::instance();
}

} // namespace api

#define API_REGISTER_SCRIPT_IMPL(script_class, counter)                        \
    namespace                                                                  \
    {                                                                          \
    struct script_registrar_##counter                                         \
    {                                                                          \
        script_registrar_##counter()                                           \
        {                                                                      \
            static script_class s_instance;                                    \
        }                                                                      \
    };                                                                         \
    static const script_registrar_##counter s_script_registrar_##counter;      \
    }

#define API_REGISTER_SCRIPT_EXPAND(script_class, counter)                      \
    API_REGISTER_SCRIPT_IMPL(script_class, counter)

#define API_REGISTER_SCRIPT(script_class)                                      \
    API_REGISTER_SCRIPT_EXPAND(script_class, __COUNTER__)
