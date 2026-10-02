#include "api/service_registry.hpp"

namespace api
{

service_registry& service_registry::instance()
{
    static service_registry s_instance;

    return s_instance;
}

bool service_registry::has_service(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return services_.find(name) != services_.end();
}

bool service_registry::remove_service(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex_);

    return services_.erase(name) > 0;
}

std::vector<std::string> service_registry::get_service_names() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;

    names.reserve(services_.size());

    for (const auto& [name, _] : services_)
    {
        names.push_back(name);
    }

    return names;
}

} // namespace api
