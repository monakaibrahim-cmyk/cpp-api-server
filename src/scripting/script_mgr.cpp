#include <scripting/script_mgr.h>
#include <core/logger.h>
#include <server/router.h>

namespace api
{

server_script::server_script(std::string name) : script_object(std::move(name))
{
    script_mgr::instance().register_server_script(this);
}

request_script::request_script(std::string name)
    : script_object(std::move(name))
{
    script_mgr::instance().register_request_script(this);
}

lua_script::lua_script(std::string name) : script_object(std::move(name))
{
    script_mgr::instance().register_lua_script(this);
}

handler_script::handler_script(std::string name)
    : script_object(std::move(name))
{
    script_mgr::instance().register_handler_script(this);
}

module_script::module_script(std::string name, std::string version,
                             std::string description)
    : script_object(std::move(name)), version_(std::move(version)),
      description_(std::move(description))
{
    script_mgr::instance().register_module_script(this);
}

script_mgr &script_mgr::instance()
{
    static script_mgr static_instance;

    return static_instance;
}

void script_mgr::initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);

    LOG_INFO("modules",
             "ScriptMgr initialized with "
                 << (server_scripts_.size() + request_scripts_.size() +
                     lua_scripts_.size() + handler_scripts_.size() +
                     module_scripts_.size())
                 << " script(s)");
}

void script_mgr::register_server_script(server_script *script_instance)
{
    std::lock_guard<std::mutex> lock(mutex_);

    server_scripts_.push_back(script_instance);

    LOG_DEBUG("modules",
              "Registered ServerScript: " << script_instance->get_name());
}

void script_mgr::register_request_script(request_script *script_instance)
{
    std::lock_guard<std::mutex> lock(mutex_);

    request_scripts_.push_back(script_instance);

    LOG_DEBUG("modules",
              "Registered RequestScript: " << script_instance->get_name());
}

void script_mgr::register_lua_script(lua_script *script_instance)
{
    std::lock_guard<std::mutex> lock(mutex_);

    lua_scripts_.push_back(script_instance);

    LOG_DEBUG("modules",
              "Registered LuaScript: " << script_instance->get_name());
}

void script_mgr::register_handler_script(handler_script *script_instance)
{
    std::lock_guard<std::mutex> lock(mutex_);

    handler_scripts_.push_back(script_instance);

    LOG_DEBUG("modules",
              "Registered HandlerScript: " << script_instance->get_name());
}

void script_mgr::register_module_script(module_script *script_instance)
{
    std::lock_guard<std::mutex> lock(mutex_);

    module_scripts_.push_back(script_instance);

    LOG_INFO("modules",
             "Loaded module: " << script_instance->get_name() << " v"
                               << script_instance->get_version() << " ("
                               << script_instance->get_description() << ")");
}

void script_mgr::on_config_load(ServerConfig &configuration)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto *server_script_instance : server_scripts_)
    {
        try
        {
            server_script_instance->on_config_load(configuration);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << server_script_instance->get_name()
                          << "] on_config_load: " << exception.what());
        }
    }

    for (auto *module_script_instance : module_scripts_)
    {
        try
        {
            module_script_instance->on_config_load(configuration);
            module_script_instance->on_init(configuration);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << module_script_instance->get_name()
                          << "] on_config_load: " << exception.what());
        }
    }
}

void script_mgr::on_config_reload(ServerConfig &configuration)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto *server_script_instance : server_scripts_)
    {
        try
        {
            server_script_instance->on_config_reload(configuration);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << server_script_instance->get_name()
                          << "] on_config_reload: " << exception.what());
        }
    }
}

void script_mgr::on_handlers_register(router &server_router)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto *handler_script_instance : handler_scripts_)
    {
        try
        {
            handler_script_instance->on_handlers_register(server_router);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << handler_script_instance->get_name()
                          << "] on_handlers_register: " << exception.what());
        }
    }

    for (auto *module_script_instance : module_scripts_)
    {
        try
        {
            module_script_instance->on_handlers_register(server_router);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << module_script_instance->get_name()
                          << "] on_handlers_register: " << exception.what());
        }
    }
}

void script_mgr::on_lua_init(lua_engine &lua_engine_instance)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto *lua_script_instance : lua_scripts_)
    {
        try
        {
            lua_script_instance->on_lua_init(lua_engine_instance);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules", "[" << lua_script_instance->get_name()
                                     << "] on_lua_init: " << exception.what());
        }
    }

    for (auto *module_script_instance : module_scripts_)
    {
        try
        {
            module_script_instance->on_lua_init(lua_engine_instance);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules", "[" << module_script_instance->get_name()
                                     << "] on_lua_init: " << exception.what());
        }
    }
}

bool script_mgr::on_request_override(crow::request &request,
                                     crow::response &response)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto *request_script_instance : request_scripts_)
    {
        try
        {
            if (request_script_instance->on_request_override(request, response))
            {
                return true;
            }
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << request_script_instance->get_name()
                          << "] on_request_override: " << exception.what());
        }
    }

    for (auto *module_script_instance : module_scripts_)
    {
        try
        {
            if (module_script_instance->on_request_override(request, response))
            {
                return true;
            }
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << module_script_instance->get_name()
                          << "] on_request_override: " << exception.what());
        }
    }

    return false;
}

void script_mgr::on_before_request(crow::request &request,
                                   crow::response &response)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto *request_script_instance : request_scripts_)
    {
        try
        {
            request_script_instance->on_before_request(request, response);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << request_script_instance->get_name()
                          << "] on_before_request: " << exception.what());
        }
    }

    for (auto *module_script_instance : module_scripts_)
    {
        try
        {
            module_script_instance->on_before_request(request, response);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << module_script_instance->get_name()
                          << "] on_before_request: " << exception.what());
        }
    }
}

void script_mgr::on_after_request(crow::request &request,
                                  crow::response &response,
                                  double duration_milliseconds)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto *request_script_instance : request_scripts_)
    {
        try
        {
            request_script_instance->on_after_request(request, response,
                                                      duration_milliseconds);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << request_script_instance->get_name()
                          << "] on_after_request: " << exception.what());
        }
    }

    for (auto *module_script_instance : module_scripts_)
    {
        try
        {
            module_script_instance->on_after_request(request, response,
                                                     duration_milliseconds);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << module_script_instance->get_name()
                          << "] on_after_request: " << exception.what());
        }
    }
}

void script_mgr::on_server_startup(api_server &server_instance)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto *server_script_instance : server_scripts_)
    {
        try
        {
            server_script_instance->on_server_startup(server_instance);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << server_script_instance->get_name()
                          << "] on_server_startup: " << exception.what());
        }
    }

    for (auto *module_script_instance : module_scripts_)
    {
        try
        {
            module_script_instance->on_server_startup(server_instance);
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << module_script_instance->get_name()
                          << "] on_server_startup: " << exception.what());
        }
    }
}

void script_mgr::on_server_shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto *server_script_instance : server_scripts_)
    {
        try
        {
            server_script_instance->on_server_shutdown();
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << server_script_instance->get_name()
                          << "] on_server_shutdown: " << exception.what());
        }
    }

    for (auto *module_script_instance : module_scripts_)
    {
        try
        {
            module_script_instance->on_server_shutdown();
        }
        catch (const std::exception &exception)
        {
            LOG_ERROR("modules",
                      "[" << module_script_instance->get_name()
                          << "] on_server_shutdown: " << exception.what());
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

    for (auto *server_script_instance : server_scripts_)
    {
        names.push_back(server_script_instance->get_name());
    }

    for (auto *request_script_instance : request_scripts_)
    {
        names.push_back(request_script_instance->get_name());
    }

    for (auto *lua_script_instance : lua_scripts_)
    {
        names.push_back(lua_script_instance->get_name());
    }

    for (auto *handler_script_instance : handler_scripts_)
    {
        names.push_back(handler_script_instance->get_name());
    }

    for (auto *module_script_instance : module_scripts_)
    {
        names.push_back(module_script_instance->get_name());
    }

    return names;
}

std::vector<module_script *> script_mgr::get_module_scripts() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return module_scripts_;
}

} // namespace api
