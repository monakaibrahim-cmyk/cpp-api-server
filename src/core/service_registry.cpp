#include "api/service_registry.h"

namespace api
{

service_registry &service_registry::instance()
{
    static service_registry static_instance;

    return static_instance;
}

bool service_registry::has_service(const std::string &service_name) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return services_.find(service_name) != services_.end();
}

bool service_registry::remove_service(const std::string &service_name)
{
    std::lock_guard<std::mutex> lock(mutex_);

    return services_.erase(service_name) > 0;
}

std::vector<std::string> service_registry::get_service_names() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> service_names;

    service_names.reserve(services_.size());

    for (const auto &[service_name, _] : services_)
    {
        service_names.push_back(service_name);
    }

    return service_names;
}

} // namespace api
