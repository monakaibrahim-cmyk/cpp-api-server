#include "api/script_mgr.hpp"
#include "api/logger.hpp"
#include "api/router.hpp"

namespace api
{

server_script::server_script(std::string name)
    : script_object(std::move(name))
{
    script_mgr::instance().register_server_script(this);
}

request_script::request_script(std::string name)
    : script_object(std::move(name))
{
    script_mgr::instance().register_request_script(this);
}

lua_script::lua_script(std::string name)
    : script_object(std::move(name))
{
    script_mgr::instance().register_lua_script(this);
}

handler_script::handler_script(std::string name)
    : script_object(std::move(name))
{
    script_mgr::instance().register_handler_script(this);
}

module_script::module_script(
    std::string name,
    std::string version,
    std::string description
)
    : script_object(std::move(name))
    , version_(std::move(version))
    , description_(std::move(description))
{
    script_mgr::instance().register_module_script(this);
}

script_mgr& script_mgr::instance()
{
    static script_mgr s_instance;

    return s_instance;
}

void script_mgr::initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);

    LOG_INFO(
        "modules",
        "ScriptMgr initialized with "
        << (server_scripts_.size() + request_scripts_.size() +
            lua_scripts_.size() + handler_scripts_.size() +
            module_scripts_.size())
        << " script(s)"
    );
}

void script_mgr::register_server_script(server_script* script)
{
    std::lock_guard<std::mutex> lock(mutex_);

    server_scripts_.push_back(script);

    LOG_DEBUG("modules", "Registered ServerScript: " << script->get_name());
}

void script_mgr::register_request_script(request_script* script)
{
    std::lock_guard<std::mutex> lock(mutex_);

    request_scripts_.push_back(script);

    LOG_DEBUG("modules", "Registered RequestScript: " << script->get_name());
}

void script_mgr::register_lua_script(lua_script* script)
{
    std::lock_guard<std::mutex> lock(mutex_);

    lua_scripts_.push_back(script);

    LOG_DEBUG("modules", "Registered LuaScript: " << script->get_name());
}

void script_mgr::register_handler_script(handler_script* script)
{
    std::lock_guard<std::mutex> lock(mutex_);

    handler_scripts_.push_back(script);

    LOG_DEBUG("modules", "Registered HandlerScript: " << script->get_name());
}

void script_mgr::register_module_script(module_script* script)
{
    std::lock_guard<std::mutex> lock(mutex_);

    module_scripts_.push_back(script);

    LOG_INFO(
        "modules",
        "Loaded module: " << script->get_name()
        << " v" << script->get_version()
        << " (" << script->get_description() << ")"
    );
}

void script_mgr::on_config_load(server_config& cfg)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto* s : server_scripts_)
    {
        try
        {
            s->on_config_load(cfg);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << s->get_name() << "] on_config_load: " << e.what());
        }
    }

    for (auto* m : module_scripts_)
    {
        try
        {
            m->on_config_load(cfg);
            m->on_init(cfg);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << m->get_name() << "] on_config_load: " << e.what());
        }
    }
}

void script_mgr::on_config_reload(server_config& cfg)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto* s : server_scripts_)
    {
        try
        {
            s->on_config_reload(cfg);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << s->get_name() << "] on_config_reload: " << e.what());
        }
    }
}

void script_mgr::on_handlers_register(router& rtr)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto* s : handler_scripts_)
    {
        try
        {
            s->on_handlers_register(rtr);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << s->get_name() << "] on_handlers_register: " << e.what());
        }
    }

    for (auto* m : module_scripts_)
    {
        try
        {
            m->on_handlers_register(rtr);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << m->get_name() << "] on_handlers_register: " << e.what());
        }
    }
}

void script_mgr::on_lua_init(lua_engine& lua)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto* s : lua_scripts_)
    {
        try
        {
            s->on_lua_init(lua);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << s->get_name() << "] on_lua_init: " << e.what());
        }
    }

    for (auto* m : module_scripts_)
    {
        try
        {
            m->on_lua_init(lua);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << m->get_name() << "] on_lua_init: " << e.what());
        }
    }
}

bool script_mgr::on_request_override(crow::request& req, crow::response& res)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto* s : request_scripts_)
    {
        try
        {
            if (s->on_request_override(req, res))
            {
                return true;
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << s->get_name() << "] on_request_override: " << e.what());
        }
    }

    for (auto* m : module_scripts_)
    {
        try
        {
            if (m->on_request_override(req, res))
            {
                return true;
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << m->get_name() << "] on_request_override: " << e.what());
        }
    }

    return false;
}

void script_mgr::on_before_request(crow::request& req, crow::response& res)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto* s : request_scripts_)
    {
        try
        {
            s->on_before_request(req, res);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << s->get_name() << "] on_before_request: " << e.what());
        }
    }

    for (auto* m : module_scripts_)
    {
        try
        {
            m->on_before_request(req, res);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << m->get_name() << "] on_before_request: " << e.what());
        }
    }
}

void script_mgr::on_after_request(
    crow::request& req,
    crow::response& res,
    double duration_ms
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto* s : request_scripts_)
    {
        try
        {
            s->on_after_request(req, res, duration_ms);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << s->get_name() << "] on_after_request: " << e.what());
        }
    }

    for (auto* m : module_scripts_)
    {
        try
        {
            m->on_after_request(req, res, duration_ms);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << m->get_name() << "] on_after_request: " << e.what());
        }
    }
}

void script_mgr::on_server_startup(api_server& server)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto* s : server_scripts_)
    {
        try
        {
            s->on_server_startup(server);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << s->get_name() << "] on_server_startup: " << e.what());
        }
    }

    for (auto* m : module_scripts_)
    {
        try
        {
            m->on_server_startup(server);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << m->get_name() << "] on_server_startup: " << e.what());
        }
    }
}

void script_mgr::on_server_shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto* s : server_scripts_)
    {
        try
        {
            s->on_server_shutdown();
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << s->get_name() << "] on_server_shutdown: " << e.what());
        }
    }

    for (auto* m : module_scripts_)
    {
        try
        {
            m->on_server_shutdown();
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("modules", "[" << m->get_name() << "] on_server_shutdown: " << e.what());
        }
    }
}

size_t script_mgr::get_script_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return server_scripts_.size() + request_scripts_.size() +
           lua_scripts_.size() + handler_scripts_.size() +
           module_scripts_.size();
}

std::vector<std::string> script_mgr::get_script_names() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;

    for (auto* s : server_scripts_)
    {
        names.push_back(s->get_name());
    }

    for (auto* s : request_scripts_)
    {
        names.push_back(s->get_name());
    }

    for (auto* s : lua_scripts_)
    {
        names.push_back(s->get_name());
    }

    for (auto* s : handler_scripts_)
    {
        names.push_back(s->get_name());
    }

    for (auto* m : module_scripts_)
    {
        names.push_back(m->get_name());
    }

    return names;
}

std::vector<module_script*> script_mgr::get_module_scripts() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return module_scripts_;
}

} // namespace api
