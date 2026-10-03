#include "api/middleware.h"
#include "api/connection_tracker.h"
#include "api/logger.h"
#include "api/script_mgr.h"

#include <iomanip>

namespace api
{

std::shared_ptr<connection_tracker> RequestLogger::static_tracker_ = nullptr;
std::vector<std::string> CorsHandler::static_allowed_origins_ = {"*"};

static std::string resolve_remote_ip(const crow::request &request)
{
    std::string x_forwarded_for = request.get_header_value("X-Forwarded-For");

    if (!x_forwarded_for.empty())
    {
        auto comma_position = x_forwarded_for.find(',');

        if (comma_position != std::string::npos)
        {
            x_forwarded_for = x_forwarded_for.substr(0, comma_position);
        }

        size_t first_non_space = x_forwarded_for.find_first_not_of(" \t\r\n");
        size_t last_non_space = x_forwarded_for.find_last_not_of(" \t\r\n");

        if (first_non_space != std::string::npos &&
            last_non_space != std::string::npos)
        {
            return x_forwarded_for.substr(first_non_space,
                                          last_non_space - first_non_space + 1);
        }
    }

    std::string x_real_ip = request.get_header_value("X-Real-IP");

    if (!x_real_ip.empty())
    {
        return x_real_ip;
    }

    if (!request.remote_ip_address.empty())
    {
        return request.remote_ip_address;
    }

    return "unknown";
}

void RequestLogger::before_handle(crow::request &request,
                                  crow::response &response,
                                  Context &middleware_context)
{
    middleware_context.start_time = std::chrono::steady_clock::now();

    if (static_tracker_)
    {
        std::string host_name = request.get_header_value("Host");

        if (host_name.empty())
        {
            host_name = "127.0.0.1";
        }

        std::string raw_url_string =
            request.raw_url.empty() ? request.url : request.raw_url;
        std::string complete_url = "http://" + host_name + raw_url_string;
        std::string client_ip_address = resolve_remote_ip(request);

        middleware_context.connection_id = static_tracker_->on_request_start(
            client_ip_address, crow::method_name(request.method), request.url,
            complete_url);
    }

    if (s_script_mgr().on_request_override(request, response))
    {
        response.end();

        return;
    }

    s_script_mgr().on_before_request(request, response);
}

void RequestLogger::after_handle(crow::request &request,
                                 crow::response &response,
                                 Context &middleware_context)
{
    auto end_time = std::chrono::steady_clock::now();
    double duration_milliseconds = 0.0;

    if (middleware_context.connection_id != 0)
    {
        duration_milliseconds = std::chrono::duration<double, std::milli>(
                                    end_time - middleware_context.start_time)
                                    .count();
    }

    std::string client_ip_address = resolve_remote_ip(request);

    if (static_tracker_)
    {
        if (middleware_context.connection_id != 0)
        {
            static_tracker_->on_request_end(middleware_context.connection_id,
                                            response.code,
                                            duration_milliseconds);
            middleware_context.connection_id = 0;
        }
        else
        {
            std::string host_name = request.get_header_value("Host");

            if (host_name.empty())
            {
                host_name = "127.0.0.1";
            }

            std::string raw_url_string =
                request.raw_url.empty() ? request.url : request.raw_url;
            std::string complete_url = "http://" + host_name + raw_url_string;

            static_tracker_->record_completed_request(
                client_ip_address, crow::method_name(request.method),
                request.url, complete_url, response.code,
                duration_milliseconds);
        }
    }

    s_script_mgr().on_after_request(request, response, duration_milliseconds);

    LOG_INFO("network", client_ip_address
                            << " - " << crow::method_name(request.method) << " "
                            << request.url << " " << response.code << " ("
                            << std::fixed << std::setprecision(1)
                            << duration_milliseconds << "ms)");
}

void RequestLogger::set_tracker(
    std::shared_ptr<connection_tracker> tracker_instance)
{
    static_tracker_ = tracker_instance;
}

void CorsHandler::before_handle(crow::request &request,
                                crow::response &response,
                                Context &middleware_context)
{
    if (request.method == crow::HTTPMethod::OPTIONS)
    {
        response.code = 204;
        response.add_header("Access-Control-Allow-Origin",
                            static_allowed_origins_.empty()
                                ? "*"
                                : static_allowed_origins_.front());
        response.add_header("Access-Control-Allow-Methods",
                            "GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS");
        response.add_header("Access-Control-Allow-Headers",
                            "Content-Type, Authorization");
        response.add_header("Access-Control-Max-Age", "86400");
        response.end();
    }
}

void CorsHandler::after_handle(crow::request &request, crow::response &response,
                               Context &middleware_context)
{
    if (!static_allowed_origins_.empty())
    {
        response.add_header("Access-Control-Allow-Origin",
                            static_allowed_origins_.front());
    }
    else
    {
        response.add_header("Access-Control-Allow-Origin", "*");
    }
}

void CorsHandler::set_allowed_origins(std::vector<std::string> allowed_origins)
{
    static_allowed_origins_ = std::move(allowed_origins);
}

} // namespace api
