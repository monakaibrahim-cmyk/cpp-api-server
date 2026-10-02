#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace api
{

class service_registry
{
public:
    static service_registry& instance();

    template <typename T>
    void register_service(
        const std::string& name,
        std::shared_ptr<T> service)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        services_[name] = std::static_pointer_cast<void>(service);
    }

    template <typename T>
    void override_service(
        const std::string& name,
        std::shared_ptr<T> service)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        services_[name] = std::static_pointer_cast<void>(service);
    }

    template <typename T>
    std::shared_ptr<T> get_service(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = services_.find(name);

        if (it != services_.end())
        {
            return std::static_pointer_cast<T>(it->second);
        }

        return nullptr;
    }

    bool has_service(const std::string& name) const;
    bool remove_service(const std::string& name);
    std::vector<std::string> get_service_names() const;

private:
    service_registry() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<void>> services_;
};

inline service_registry& s_services()
{
    return service_registry::instance();
}

} // namespace api
