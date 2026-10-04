#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace api
{

/**
 * @brief Thread-safe dependency injection registry storing shared services by
 * name.
 *
 * Implements a service locator and dependency injection container storing
 * type-erased
 * @c std::shared_ptr<void> references indexed by unique string identifiers.
 * Allows decoupled modules and plugins to publish, discover, and override
 * services dynamically without compile-time coupling between disparate
 * components.
 *
 * @par Registration and Lookup Example
 * @code{.cpp}
 * struct IDatabaseConnector
 * {
 *     virtual ~IDatabaseConnector() = default;
 *     virtual bool execute_query(const std::string& query) = 0;
 * };
 *
 * class PostgresConnector : public IDatabaseConnector
 * {
 * public:
 *     bool execute_query(const std::string& query) override { return true; }
 * };
 *
 * // Register service implementation
 * auto connector = std::make_shared<PostgresConnector>();
 * api::s_services().register_service<IDatabaseConnector>("db.primary",
 * connector);
 *
 * // Retrieve service across another module
 * auto resolved_service =
 * api::s_services().get_service<IDatabaseConnector>("db.primary"); if
 * (resolved_service)
 * {
 *     resolved_service->execute_query("SELECT 1;");
 * }
 * @endcode
 *
 * @thread_safety All public member methods lock an internal @c std::mutex,
 * ensuring safe concurrent registration, retrieval, and removal across worker
 * threads.
 *
 * @headerfile api/service_registry.h
 */
class service_registry
{
  public:
    /**
     * @brief Accesses the global singleton service registry instance.
     *
     * @return Reference to the @ref service_registry singleton.
     */
    static service_registry &instance();

    /**
     * @brief Registers a new named service with the registry.
     *
     * Stores the service instance under @p service_name. If a service already
     * exists with the same key, it is replaced with @p service_instance.
     *
     * @tparam T Service concrete or interface type.
     * @param service_name Unique string key identifying the service.
     * @param service_instance Shared pointer to the service instance.
     */
    template <typename T>
    void register_service(const std::string &service_name,
                          std::shared_ptr<T> service_instance)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        services_[service_name] =
            std::static_pointer_cast<void>(service_instance);
    }

    /**
     * @brief Overrides an existing named service with a new instance.
     *
     * Replaces any existing service mapped to @p service_name with the provided
     * @p service_instance. Useful for unit testing, mocking, or plugin
     * replacement.
     *
     * @tparam T Service concrete or interface type.
     * @param service_name Unique string key identifying the service to
     * overwrite.
     * @param service_instance New shared pointer to the replacement service
     * instance.
     */
    template <typename T>
    void override_service(const std::string &service_name,
                          std::shared_ptr<T> service_instance)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        services_[service_name] =
            std::static_pointer_cast<void>(service_instance);
    }

    /**
     * @brief Retrieves a named service cast back to the requested type.
     *
     * Looks up @p service_name in the registry and statically casts the
     * underlying pointer to @c std::shared_ptr<T>.
     *
     * @tparam T Service concrete or interface type to cast to.
     * @param service_name Unique string key identifying the registered service.
     * @return Shared pointer to @c T, or @c nullptr if the service key was not
     * found.
     */
    template <typename T>
    std::shared_ptr<T> get_service(const std::string &service_name) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto iterator = services_.find(service_name);

        if (iterator != services_.end())
        {
            return std::static_pointer_cast<T>(iterator->second);
        }

        return nullptr;
    }

    /**
     * @brief Checks if a service exists under the given name.
     *
     * @param service_name Service identifier key.
     * @return @c true if a service is registered under @p service_name; @c
     * false otherwise.
     */
    bool has_service(const std::string &service_name) const;

    /**
     * @brief Removes a named service from the registry.
     *
     * @param service_name Service identifier key to remove.
     * @return @c true if the service was present and removed; @c false if it
     * did not exist.
     */
    bool remove_service(const std::string &service_name);

    /**
     * @brief Retrieves the names of all currently registered services.
     *
     * @return Vector of registered service name strings.
     */
    std::vector<std::string> get_service_names() const;

  private:
    service_registry() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<void>> services_;
};

/**
 * @brief Helper function returning reference to global @ref service_registry
 * singleton.
 *
 * @return Reference to the @ref service_registry singleton instance.
 */
inline service_registry &s_services() { return service_registry::instance(); }

} // namespace api
