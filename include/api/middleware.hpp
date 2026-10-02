#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <crow.h>

namespace api
{

class connection_tracker;

struct request_logger
{
    struct context
    {
        std::chrono::steady_clock::time_point start_time;
        uint64_t conn_id = 0;
    };

    void before_handle(
        crow::request& req,
        crow::response& res,
        context& ctx);

    void after_handle(
        crow::request& req,
        crow::response& res,
        context& ctx);

    static void set_tracker(std::shared_ptr<connection_tracker> tracker);

private:
    static std::shared_ptr<connection_tracker> s_tracker_;
};

struct cors_handler
{
    struct context
    {
    };

    void before_handle(
        crow::request& req,
        crow::response& res,
        context& ctx);

    void after_handle(
        crow::request& req,
        crow::response& res,
        context& ctx);

    static void set_allowed_origins(std::vector<std::string> origins);

private:
    static std::vector<std::string> s_origins_;
};

using api_app_t = crow::App<request_logger, cors_handler>;

} // namespace api
