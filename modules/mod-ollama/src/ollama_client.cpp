#include <ollama_client.h>
#include <core/logger.h>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <chrono>
#include <unordered_set>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace api
{

ollama_client::ollama_client(std::string host, uint16_t port,
                             int timeout_seconds)
    : host_(std::move(host)), port_(port), timeout_seconds_(timeout_seconds)
{
}

bool ollama_client::http_request(const std::string &method,
                                 const std::string &target,
                                 const std::string &body, int timeout_sec,
                                 std::string &response_body, int &status_code,
                                 std::string &error_msg)
{
    try
    {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        int effective_timeout = timeout_sec > 0 ? timeout_sec : timeout_seconds_;
        stream.expires_after(std::chrono::seconds(effective_timeout));

        beast::error_code ec;
        auto const results =
            resolver.resolve(host_, std::to_string(port_), ec);
        if (ec)
        {
            error_msg = "Resolution error for " + host_ + ":" +
                        std::to_string(port_) + " - " + ec.message();
            return false;
        }

        stream.connect(results, ec);
        if (ec)
        {
            error_msg = "Could not connect to Ollama daemon at " + host_ +
                        ":" + std::to_string(port_) + " - " + ec.message();
            return false;
        }

        http::verb verb =
            (method == "POST") ? http::verb::post : http::verb::get;
        http::request<http::string_body> req{verb, target, 11};
        req.set(http::field::host, host_ + ":" + std::to_string(port_));
        req.set(http::field::user_agent, "API-cli-ollama/1.0");
        req.set(http::field::accept, "application/json");

        if (method == "POST")
        {
            req.set(http::field::content_type, "application/json");
            req.body() = body;
            req.prepare_payload();
        }

        http::write(stream, req, ec);
        if (ec)
        {
            error_msg = "Failed writing request to Ollama: " + ec.message();
            return false;
        }

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res, ec);
        if (ec)
        {
            error_msg = "Failed reading response from Ollama: " + ec.message();
            return false;
        }

        status_code = res.result_int();
        response_body = res.body();

        beast::error_code close_ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, close_ec);

        return true;
    }
    catch (const std::exception &ex)
    {
        error_msg = std::string("HTTP client exception: ") + ex.what();
        return false;
    }
}

bool ollama_client::is_available(std::string &out_version,
                                 std::string &out_error)
{
    std::string response_body;
    int status_code = 0;

    bool ok = http_request("GET", "/api/version", "", 5, response_body,
                           status_code, out_error);
    if (!ok || status_code != 200)
    {
        if (out_error.empty())
        {
            out_error =
                "Ollama daemon returned HTTP " + std::to_string(status_code);
        }
        return false;
    }

    auto parsed = crow::json::load(response_body);
    if (parsed && parsed.has("version"))
    {
        out_version = parsed["version"].s();
    }
    else
    {
        out_version = "unknown";
    }

    return true;
}

std::vector<OllamaAgent> ollama_client::list_agents(std::string &out_error)
{
    std::vector<OllamaAgent> agents;
    std::string response_body;
    int status_code = 0;

    // Check which models are actively running in memory via /api/ps
    std::unordered_set<std::string> running_models;
    std::string ps_response;
    int ps_status = 0;
    std::string ps_err;
    if (http_request("GET", "/api/ps", "", 5, ps_response, ps_status, ps_err) &&
        ps_status == 200)
    {
        auto ps_json = crow::json::load(ps_response);
        if (ps_json && ps_json.has("models"))
        {
            for (const auto &item : ps_json["models"])
            {
                if (item.has("name"))
                {
                    running_models.insert(std::string(item["name"].s()));
                }
                if (item.has("model"))
                {
                    running_models.insert(std::string(item["model"].s()));
                }
            }
        }
    }

    // Auto-discover all installed local models via /api/tags
    bool ok = http_request("GET", "/api/tags", "", 10, response_body,
                           status_code, out_error);
    if (!ok || status_code != 200)
    {
        if (out_error.empty())
        {
            out_error = "Ollama /api/tags returned HTTP " +
                        std::to_string(status_code) + ": " + response_body;
        }
        return agents;
    }

    auto parsed = crow::json::load(response_body);
    if (!parsed)
    {
        out_error = "Failed to parse JSON response from Ollama /api/tags";
        return agents;
    }

    if (!parsed.has("models"))
    {
        return agents;
    }

    for (const auto &item : parsed["models"])
    {
        OllamaAgent agent;
        if (item.has("name"))
        {
            agent.name = item["name"].s();
        }
        if (item.has("model"))
        {
            agent.model = item["model"].s();
        }
        else
        {
            agent.model = agent.name;
        }
        if (item.has("modified_at"))
        {
            agent.modified_at = item["modified_at"].s();
        }
        if (item.has("size"))
        {
            agent.size_bytes = item["size"].u();
        }

        if (item.has("details"))
        {
            const auto &details = item["details"];
            if (details.has("family"))
            {
                agent.family = details["family"].s();
            }
            if (details.has("parameter_size"))
            {
                agent.parameter_size = details["parameter_size"].s();
            }
            if (details.has("format"))
            {
                agent.format = details["format"].s();
            }
            if (details.has("quantization_level"))
            {
                agent.quantization_level = details["quantization_level"].s();
            }
        }

        if (running_models.contains(agent.name) ||
            running_models.contains(agent.model))
        {
            agent.is_running = true;
        }

        agents.push_back(std::move(agent));
    }

    return agents;
}

ChatResult ollama_client::chat(const std::string &model,
                              const std::vector<ChatMessage> &messages,
                              const std::string &system_prompt)
{
    ChatResult result;
    result.agent = model;

    crow::json::wvalue request_json;
    request_json["model"] = model;
    request_json["stream"] = false;

    std::vector<crow::json::wvalue> messages_array;

    if (!system_prompt.empty())
    {
        crow::json::wvalue system_message;
        system_message["role"] = "system";
        system_message["content"] = system_prompt;
        messages_array.push_back(std::move(system_message));
    }

    for (const auto &message : messages)
    {
        crow::json::wvalue message_item;
        message_item["role"] = message.role;
        message_item["content"] = message.content;
        messages_array.push_back(std::move(message_item));
    }

    request_json["messages"] = std::move(messages_array);

    std::string payload_body = request_json.dump();
    std::string response_body;
    int status_code = 0;
    std::string error_message;

    bool ok = http_request("POST", "/api/chat", payload_body, timeout_seconds_,
                           response_body, status_code, error_message);
    if (!ok)
    {
        result.success = false;
        result.error = error_message;
        return result;
    }

    if (status_code != 200)
    {
        result.success = false;
        result.error = "Ollama returned HTTP " + std::to_string(status_code) +
                       ": " + response_body;
        return result;
    }

    auto parsed = crow::json::load(response_body);
    if (!parsed)
    {
        result.success = false;
        result.error = "Failed to parse JSON response from Ollama /api/chat";
        return result;
    }

    if (!parsed.has("message"))
    {
        result.success = false;
        result.error = "Response missing 'message' field: " + response_body;
        return result;
    }

    const auto &message_obj = parsed["message"];
    if (message_obj.has("content"))
    {
        result.response = message_obj["content"].s();
    }

    if (parsed.has("total_duration"))
    {
        result.total_duration_ns = parsed["total_duration"].i();
    }
    if (parsed.has("eval_count"))
    {
        result.eval_count = static_cast<int>(parsed["eval_count"].i());
    }

    result.success = true;
    return result;
}

} // namespace api
