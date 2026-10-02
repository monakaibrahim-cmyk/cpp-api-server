#pragma once

#include <atomic>
#include <thread>

#include "api/config.hpp"
#include "api/middleware.hpp"

namespace api
{

class api_server
{
public:
    explicit api_server(const server_config& cfg);
    ~api_server();

    void start();
    void stop();
    bool is_running() const;
    api_app_t& app();

private:
    server_config config_;
    api_app_t app_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
};

} // namespace api
