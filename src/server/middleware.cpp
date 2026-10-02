#include "api/middleware.hpp"
#include "api/connection_tracker.hpp"
#include "api/logger.hpp"
#include "api/script_mgr.hpp"

#include <iomanip>

namespace api
{

std::shared_ptr<connection_tracker> request_logger::s_tracker_ = nullptr;
std::vector<std::string> cors_handler::s_origins_ = {"*"};

static std::string resolve_remote_ip(const crow::request& req)
{
    std::string xff = req.get_header_value("X-Forwarded-For");

    if (!xff.empty())
    {
        auto comma = xff.find(',');

        if (comma != std::string::npos)
        {
            xff = xff.substr(0, comma);
        }

        size_t first = xff.find_first_not_of(" \t\r\n");
        size_t last = xff.find_last_not_of(" \t\r\n");

        if (first != std::string::npos && last != std::string::npos)
        {
            return xff.substr(first, last - first + 1);
        }
    }

    std::string xri = req.get_header_value("X-Real-IP");

    if (!xri.empty())
    {
        return xri;
    }

    if (!req.remote_ip_address.empty())
    {
        return req.remote_ip_address;
    }

    return "unknown";
}

void request_logger::before_handle(
    crow::request& req,
    crow::response& res,
    context& ctx
)
{
    ctx.start_time = std::chrono::steady_clock::now();

    if (s_tracker_)
    {
        std::string host = req.get_header_value("Host");

        if (host.empty())
        {
            host = "127.0.0.1";
        }

        std::string raw = req.raw_url.empty() ? req.url : req.raw_url;
        std::string full_url = "http://" + host + raw;
        std::string client_ip = resolve_remote_ip(req);

        ctx.conn_id = s_tracker_->on_request_start(
            client_ip,
            crow::method_name(req.method),
            req.url,
            full_url
        );
    }

    if (s_script_mgr().on_request_override(req, res))
    {
        res.end();

        return;
    }

    s_script_mgr().on_before_request(req, res);
}

void request_logger::after_handle(
    crow::request& req,
    crow::response& res,
    context& ctx
)
{
    auto end_time = std::chrono::steady_clock::now();
    double duration_ms = 0.0;

    if (ctx.conn_id != 0)
    {
        duration_ms = std::chrono::duration<double, std::milli>(end_time - ctx.start_time).count();
    }

    std::string client_ip = resolve_remote_ip(req);

    if (s_tracker_)
    {
        if (ctx.conn_id != 0)
        {
            s_tracker_->on_request_end(ctx.conn_id, res.code, duration_ms);
            ctx.conn_id = 0;
        }
        else
        {
            std::string host = req.get_header_value("Host");

            if (host.empty())
            {
                host = "127.0.0.1";
            }

            std::string raw = req.raw_url.empty() ? req.url : req.raw_url;
            std::string full_url = "http://" + host + raw;

            s_tracker_->record_completed_request(
                client_ip,
                crow::method_name(req.method),
                req.url,
                full_url,
                res.code,
                duration_ms
            );
        }
    }

    s_script_mgr().on_after_request(req, res, duration_ms);

    LOG_INFO(
        "network",
        client_ip << " - " << crow::method_name(req.method) << " " << req.url << " "
        << res.code << " (" << std::fixed << std::setprecision(1) << duration_ms << "ms)"
    );
}

void request_logger::set_tracker(std::shared_ptr<connection_tracker> tracker)
{
    s_tracker_ = tracker;
}

void cors_handler::before_handle(
    crow::request& req,
    crow::response& res,
    context& ctx
)
{
    if (req.method == crow::HTTPMethod::OPTIONS)
    {
        res.code = 204;
        res.add_header("Access-Control-Allow-Origin", s_origins_.empty() ? "*" : s_origins_.front());
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.add_header("Access-Control-Max-Age", "86400");
        res.end();
    }
}

void cors_handler::after_handle(
    crow::request& req,
    crow::response& res,
    context& ctx
)
{
    if (!s_origins_.empty())
    {
        res.add_header("Access-Control-Allow-Origin", s_origins_.front());
    }
    else
    {
        res.add_header("Access-Control-Allow-Origin", "*");
    }
}

void cors_handler::set_allowed_origins(std::vector<std::string> origins)
{
    s_origins_ = std::move(origins);
}

} // namespace api
