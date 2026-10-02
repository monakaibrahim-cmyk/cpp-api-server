#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace api
{

struct server_config
{
    uint16_t port = 8080;
    uint16_t threads = 4;
    uint16_t worker_threads = 4;
    std::string log_level = "info";
    std::string log_dir = "logs";
    bool dashboard_enabled = true;
    size_t max_body_size = 10 * 1024 * 1024;
    std::string scripts_dir = "scripts";
    std::string config_dir = "config";
    bool cache_enabled = true;
    size_t cache_max_items = 1000;
    std::vector<std::string> cors_origins = {"*"};
    std::unordered_map<std::string, std::string> log_channels;
};

server_config load_config(const std::string& lua_config_path);

} // namespace api
