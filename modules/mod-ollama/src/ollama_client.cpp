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

        int effective_timeout =
            timeout_sec > 0 ? timeout_sec : timeout_seconds_;
        stream.expires_after(std::chrono::seconds(effective_timeout));

        beast::error_code ec;
        auto const results = resolver.resolve(host_, std::to_string(port_), ec);
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

        http::verb verb = http::verb::get;
        if (method == "POST")
        {
            verb = http::verb::post;
        }
        else if (method == "DELETE")
        {
            verb = http::verb::delete_;
        }

        http::request<http::string_body> req{verb, target, 11};
        req.set(http::field::host, host_ + ":" + std::to_string(port_));
        req.set(http::field::user_agent, "API-cli-ollama/1.0");
        req.set(http::field::accept, "application/json");

        if (!body.empty() || method == "POST" || method == "DELETE")
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
                              const std::string &system_prompt,
                              const crow::json::rvalue *custom_options)
{
    ChatResult result;
    result.agent = model;

    crow::json::wvalue request_json;
    request_json["model"] = model;
    request_json["stream"] = false;

    // No token limit per user by default: num_predict = -1 (infinite generation)
    crow::json::wvalue options_val;
    options_val["num_predict"] = -1;

    if (custom_options != nullptr && custom_options->t() == crow::json::type::Object)
    {
        for (const auto &key : custom_options->keys())
        {
            const auto &v = (*custom_options)[key];
            if (v.t() == crow::json::type::Number)
            {
                options_val[key] = v.d();
            }
            else if (v.t() == crow::json::type::String)
            {
                options_val[key] = v.s();
            }
            else if (v.t() == crow::json::type::True)
            {
                options_val[key] = true;
            }
            else if (v.t() == crow::json::type::False)
            {
                options_val[key] = false;
            }
        }
    }
    request_json["options"] = std::move(options_val);

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

EmbedResult ollama_client::embed(const std::string &model,
                                 const std::vector<std::string> &inputs)
{
    EmbedResult result;
    result.model = model;

    if (inputs.empty())
    {
        result.success = true;
        return result;
    }

    crow::json::wvalue request_json;
    request_json["model"] = model;

    if (inputs.size() == 1)
    {
        request_json["input"] = inputs.front();
    }
    else
    {
        std::vector<crow::json::wvalue> inputs_array;
        for (const auto &text : inputs)
        {
            inputs_array.push_back(text);
        }
        request_json["input"] = std::move(inputs_array);
    }

    std::string payload_body = request_json.dump();
    std::string response_body;
    int status_code = 0;
    std::string error_message;

    bool ok = http_request("POST", "/api/embed", payload_body, timeout_seconds_,
                           response_body, status_code, error_message);

    // Fallback to legacy /api/embeddings endpoint if /api/embed returned 404
    if (ok && status_code == 404)
    {
        for (const auto &text : inputs)
        {
            crow::json::wvalue leg_req;
            leg_req["model"] = model;
            leg_req["prompt"] = text;
            std::string leg_body;
            int leg_code = 0;
            std::string leg_err;
            if (http_request("POST", "/api/embeddings", leg_req.dump(),
                             timeout_seconds_, leg_body, leg_code, leg_err) &&
                leg_code == 200)
            {
                auto parsed_leg = crow::json::load(leg_body);
                if (parsed_leg && parsed_leg.has("embedding"))
                {
                    std::vector<double> vector_values;
                    for (const auto &val : parsed_leg["embedding"])
                    {
                        vector_values.push_back(val.d());
                    }
                    result.embeddings.push_back(std::move(vector_values));
                }
            }
        }
        if (!result.embeddings.empty())
        {
            result.success = true;
            return result;
        }
    }

    if (!ok || status_code != 200)
    {
        result.success = false;
        result.error = error_message.empty()
                           ? "Ollama returned HTTP " +
                                 std::to_string(status_code) + ": " +
                                 response_body
                           : error_message;
        return result;
    }

    auto parsed = crow::json::load(response_body);
    if (!parsed || !parsed.has("embeddings"))
    {
        result.success = false;
        result.error = "Invalid JSON response or missing 'embeddings' field";
        return result;
    }

    for (const auto &vec_obj : parsed["embeddings"])
    {
        std::vector<double> vector_values;
        for (size_t index = 0; index < vec_obj.size(); ++index)
        {
            vector_values.push_back(vec_obj[index].d());
        }
        result.embeddings.push_back(std::move(vector_values));
    }

    if (parsed.has("total_duration"))
    {
        result.total_duration_ns = parsed["total_duration"].i();
    }
    if (parsed.has("prompt_eval_count"))
    {
        result.prompt_eval_count = static_cast<int>(parsed["prompt_eval_count"].i());
    }

    result.success = true;
    return result;
}

bool ollama_client::pull_model(const std::string &model_name,
                              std::string &out_error)
{
    crow::json::wvalue request_json;
    request_json["model"] = model_name;
    request_json["stream"] = false;

    std::string response_body;
    int status_code = 0;

    bool ok = http_request("POST", "/api/pull", request_json.dump(), 600,
                           response_body, status_code, out_error);
    return ok && status_code == 200;
}

bool ollama_client::show_model(const std::string &model_name,
                              crow::json::wvalue &out_details,
                              std::string &out_error)
{
    crow::json::wvalue request_json;
    request_json["model"] = model_name;

    std::string response_body;
    int status_code = 0;

    bool ok = http_request("POST", "/api/show", request_json.dump(), 30,
                           response_body, status_code, out_error);
    if (!ok || status_code != 200)
    {
        if (out_error.empty())
        {
            out_error =
                "Ollama returned HTTP " + std::to_string(status_code) + ": " +
                response_body;
        }
        return false;
    }

    auto parsed = crow::json::load(response_body);
    if (!parsed)
    {
        out_error = "Failed to parse model inspection JSON from Ollama";
        return false;
    }

    out_details = crow::json::wvalue(parsed);
    return true;
}

bool ollama_client::delete_model(const std::string &model_name,
                                std::string &out_error)
{
    crow::json::wvalue request_json;
    request_json["model"] = model_name;

    std::string response_body;
    int status_code = 0;

    bool ok = http_request("DELETE", "/api/delete", request_json.dump(), 30,
                           response_body, status_code, out_error);
    return ok && (status_code == 200 || status_code == 204);
}

} // namespace api
