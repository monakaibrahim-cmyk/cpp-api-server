#pragma once

#include <chrono>
#include <mutex>
#include <string>

#include <sol/sol.hpp>

namespace api
{

class metrics_collector;
class connection_tracker;

class lua_engine
{
public:
    lua_engine();
    ~lua_engine();

    void bind_core_api(metrics_collector* metrics, connection_tracker* tracker);
    void setup_package_path(const std::string& base_dir);
    void load_file(const std::string& path);

    sol::state& state();
    std::mutex& mutex();

private:
    sol::state lua_;
    std::mutex mutex_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace api
