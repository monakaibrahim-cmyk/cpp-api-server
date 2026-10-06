#include <server/router.h>
#include <core/cache.h>
#include <core/htaccess.h>
#include <core/logger.h>
#include <scripting/lua_engine.h>
#include <server/server.h>

#include <filesystem>

namespace api
{

namespace
{

void populate_lua_request(
    lua_engine &lua_engine_instance, const crow::request &request,
    const std::unordered_map<std::string, std::string> &matched_parameters,
    const HtaccessDecision &htaccess_decision_result, sol::table &request_table)
{
    sol::table request_headers_table =
        lua_engine_instance.state().create_table();

    for (const auto &pair : request.headers)
    {
        request_headers_table[pair.first] = pair.second;
    }

    request_table["headers"] = request_headers_table;
    request_table["body"] = request.body;
    request_table["url"] =
        (htaccess_decision_result.action == htaccess_action::rewrite &&
         !htaccess_decision_result.rewritten_url.empty())
            ? htaccess_decision_result.rewritten_url
            : request.url;
    request_table["raw_url"] = request.raw_url;
    request_table["method"] = crow::method_name(request.method);
    request_table["remote_addr"] = request.remote_ip_address;

    sol::table parameters_table = lua_engine_instance.state().create_table();

    for (const auto &[parameter_name, parameter_value] : matched_parameters)
    {
        parameters_table[parameter_name] = parameter_value;
    }

    request_table["params"] = parameters_table;

    sol::table query_table = lua_engine_instance.state().create_table();
    crow::query_string query_string_parser(request.raw_url);

    for (const auto &query_key : query_string_parser.keys())
    {
        char *value_pointer = query_string_parser.get(query_key);

        if (value_pointer != nullptr)
        {
            query_table[query_key] = std::string(value_pointer);
        }
    }

    request_table["query"] = query_table;

    auto content_type_header = request.get_header_value("Content-Type");

    if (!request.body.empty() &&
        content_type_header.find("application/json") != std::string::npos)
    {
        auto parsed_json = crow::json::load(request.body);

        if (parsed_json)
        {
            request_table["json"] = lua_engine::json_to_lua(
                lua_engine_instance.state(), parsed_json);
        }
        else
        {
            request_table["json"] = sol::nil;
        }
    }
    else
    {
        request_table["json"] = sol::nil;
    }
}

std::pair<std::string, size_t>
convert_path_to_crow_rule(const std::vector<std::string> &path_segments)
{
    std::string crow_pattern;
    size_t parameter_count = 0;

    for (const auto &segment : path_segments)
    {
        crow_pattern += "/";

        if (!segment.empty() && segment.front() == ':')
        {
            crow_pattern += "<string>";
            parameter_count++;
        }
        else if (segment == "*")
        {
            crow_pattern += "<path>";
            parameter_count++;
        }
        else
        {
            crow_pattern += segment;
        }
    }

    if (crow_pattern.empty())
    {
        crow_pattern = "/";
    }

    return {crow_pattern, parameter_count};
}

} // namespace

router::router(api_server &server_instance, lua_engine &lua_engine_instance)
    : server_(server_instance), lua_(lua_engine_instance)
{
    setup_lua_route_binding();
}

void router::register_native_handler(const std::string &handler_name,
                                     native_handler_type native_handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    bool already_exists =
        native_handlers_.find(handler_name) != native_handlers_.end();

    native_handlers_[handler_name] = std::move(native_handler);

    if (already_exists)
    {
        LOG_INFO("routes", "Overrode native handler: " << handler_name);
    }
    else
    {
        LOG_DEBUG("routes", "Registered native handler: " << handler_name);
    }
}

void router::override_native_handler(const std::string &handler_name,
                                     native_handler_type native_handler)
{
    register_native_handler(handler_name, std::move(native_handler));
}

bool router::has_native_handler(const std::string &handler_name) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return native_handlers_.find(handler_name) != native_handlers_.end();
}

void router::register_handler(const std::string &handler_name,
                              native_handler_type native_handler)
{
    register_native_handler(handler_name, std::move(native_handler));
}

void router::override_handler(const std::string &handler_name,
                              native_handler_type native_handler)
{
    override_native_handler(handler_name, std::move(native_handler));
}

bool router::has_handler(const std::string &handler_name) const
{
    return has_native_handler(handler_name);
}

void router::register_rest_native_handler(const std::string &handler_name,
                                          rest_native_handler_type rest_handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    rest_native_handlers_[handler_name] = std::move(rest_handler);
    LOG_DEBUG("routes", "Registered REST native handler: " << handler_name);
}

void router::register_rest_handler(const std::string &handler_name,
                                   rest_native_handler_type rest_handler)
{
    register_rest_native_handler(handler_name, std::move(rest_handler));
}

void router::bind_dynamic_crow_route(const std::string &path,
                                     crow::HTTPMethod method)
{
    std::vector<std::string> segments = split_path(path);
    auto [crow_pattern, parameter_count] = convert_path_to_crow_rule(segments);

    if (parameter_count == 1)
    {
        server_.app()
            .route_dynamic(crow_pattern)
            .methods(method)(
                [this](const crow::request &request, crow::response &response,
                       std::string /*parameter_value_1*/)
                {
                    response = dispatch_request(request);
                    response.end();
                });
    }
    else if (parameter_count == 2)
    {
        server_.app()
            .route_dynamic(crow_pattern)
            .methods(method)(
                [this](const crow::request &request, crow::response &response,
                       std::string /*parameter_value_1*/,
                       std::string /*parameter_value_2*/)
                {
                    response = dispatch_request(request);
                    response.end();
                });
    }
    else if (parameter_count == 3)
    {
        server_.app()
            .route_dynamic(crow_pattern)
            .methods(method)(
                [this](const crow::request &request, crow::response &response,
                       std::string /*parameter_value_1*/,
                       std::string /*parameter_value_2*/,
                       std::string /*parameter_value_3*/)
                {
                    response = dispatch_request(request);
                    response.end();
                });
    }
    else if (parameter_count == 4)
    {
        server_.app()
            .route_dynamic(crow_pattern)
            .methods(method)(
                [this](const crow::request &request, crow::response &response,
                       std::string /*parameter_value_1*/,
                       std::string /*parameter_value_2*/,
                       std::string /*parameter_value_3*/,
                       std::string /*parameter_value_4*/)
                {
                    response = dispatch_request(request);
                    response.end();
                });
    }
    else if (parameter_count >= 5)
    {
        server_.app()
            .route_dynamic(crow_pattern)
            .methods(method)(
                [this](const crow::request &request, crow::response &response,
                       std::string /*parameter_value_1*/,
                       std::string /*parameter_value_2*/,
                       std::string /*parameter_value_3*/,
                       std::string /*parameter_value_4*/,
                       std::string /*parameter_value_5*/)
                {
                    response = dispatch_request(request);
                    response.end();
                });
    }
}

std::vector<std::string> router::split_path(const std::string &path_string)
{
    std::vector<std::string> segments;
    std::string current_segment;

    for (char character : path_string)
    {
        if (character == '/')
        {
            if (!current_segment.empty())
            {
                segments.push_back(current_segment);
                current_segment.clear();
            }
        }
        else if (character == '?')
        {
            break;
        }
        else
        {
            current_segment.push_back(character);
        }
    }

    if (!current_segment.empty())
    {
        segments.push_back(current_segment);
    }

    return segments;
}

bool router::match_route(
    const RestRoutePattern &route_pattern,
    const std::vector<std::string> &request_segments,
    std::unordered_map<std::string, std::string> &extracted_parameters)
{
    if (route_pattern.path_segments.size() != request_segments.size())
    {
        return false;
    }

    for (size_t index = 0; index < route_pattern.path_segments.size(); ++index)
    {
        const auto &pattern_segment = route_pattern.path_segments[index];
        const auto &request_segment = request_segments[index];

        if (!pattern_segment.empty() && pattern_segment.front() == ':')
        {
            std::string parameter_name = pattern_segment.substr(1);

            extracted_parameters[parameter_name] = request_segment;
        }
        else if (pattern_segment != request_segment && pattern_segment != "*")
        {
            return false;
        }
    }

    return true;
}

crow::HTTPMethod router::parse_method(const std::string &method_string)
{
    static const std::unordered_map<std::string, crow::HTTPMethod> method_map =
        {{"GET", "GET"_method},
         {"DELETE", "DELETE"_method},
         {"HEAD", "HEAD"_method},
         {"POST", "POST"_method},
         {"PUT", "PUT"_method},

         {"OPTIONS", "OPTIONS"_method},
         {"CONNECT", "CONNECT"_method},
         {"TRACE", "TRACE"_method},

         {"PATCH", "PATCH"_method},
         {"PURGE", "PURGE"_method},
         {"COPY", "COPY"_method},
         {"LOCK", "LOCK"_method},
         {"MKCOL", "MKCOL"_method},
         {"MOVE", "MOVE"_method},
         {"PROPFIND", "PROPFIND"_method},
         {"PROPPATCH", "PROPPATCH"_method},
         {"SEARCH", "SEARCH"_method},
         {"UNLOCK", "UNLOCK"_method},
         {"BIND", "BIND"_method},
         {"REBIND", "REBIND"_method},
         {"UNBIND", "UNBIND"_method},
         {"ACL", "ACL"_method},

         {"REPORT", "REPORT"_method},
         {"MKACTIVITY", "MKACTIVITY"_method},
         {"CHECKOUT", "CHECKOUT"_method},
         {"MERGE", "MERGE"_method},

         {"MSEARCH", "MSEARCH"_method},
         {"NOTIFY", "NOTIFY"_method},
         {"SUBSCRIBE", "SUBSCRIBE"_method},
         {"UNSUBSCRIBE", "UNSUBSCRIBE"_method},

         {"MKCALENDAR", "MKCALENDAR"_method},

         {"LINK", "LINK"_method},
         {"UNLINK", "UNLINK"_method},

         {"SOURCE", "SOURCE"_method}};

    auto it = method_map.find(method_string);

    if (it != method_map.end())
    {
        return it->second;
    }

    return "GET"_method;
}

void router::setup_lua_route_binding()
{
    if (binding_registered_)
    {
        return;
    }

    server_.app().catchall_route()(
        [this](const crow::request &request, crow::response &response)
        {
            response = dispatch_request(request);
            response.end();
        });

    std::lock_guard<std::mutex> lock(lua_.mutex());

    lua_.state().set_function(
        "route",
        [this](const std::string &method_string, const std::string &path,
               sol::object handler_object, sol::optional<sol::table> options)
        {
            crow::HTTPMethod method = parse_method(method_string);
            int cache_time_to_live_seconds = 0;

            if (options && options.value().valid())
            {
                cache_time_to_live_seconds = options.value().get_or("cache", 0);

                if (cache_time_to_live_seconds <= 0)
                {
                    cache_time_to_live_seconds =
                        options.value().get_or("cache_ttl", 0);
                }
            }

            RestRoutePattern route_pattern;

            route_pattern.raw_path = path;
            route_pattern.method = method;
            route_pattern.path_segments = split_path(path);
            route_pattern.cache_time_to_live_seconds =
                cache_time_to_live_seconds;

            for (const auto &segment : route_pattern.path_segments)
            {
                if (!segment.empty() && segment.front() == ':')
                {
                    route_pattern.parameter_names.push_back(segment.substr(1));
                    route_pattern.has_parameters = true;
                }
            }

            if (handler_object.is<std::string>())
            {
                std::string handler_name = handler_object.as<std::string>();

                route_pattern.native_handler_name = handler_name;

                std::lock_guard<std::mutex> handler_lock(mutex_);
                bool handler_registered =
                    native_handlers_.find(handler_name) !=
                        native_handlers_.end() ||
                    rest_native_handlers_.find(handler_name) !=
                        rest_native_handlers_.end();

                if (!handler_registered)
                {
                    LOG_ERROR("routes", "Handler '" << handler_name
                                                    << "' not found for route: "
                                                    << method_string << " "
                                                    << path);

                    return;
                }

                rest_routes_.push_back(route_pattern);
                if (!is_hot_reload_)
                {
                    if (route_pattern.has_parameters)
                    {
                        bind_dynamic_crow_route(path, method);
                    }
                    else if (native_handlers_.find(handler_name) !=
                             native_handlers_.end())
                    {
                        auto native_handler_function =
                            native_handlers_[handler_name];

                        server_.app().route_dynamic(path).methods(method)(
                            [native_handler_function, cache_time_to_live_seconds,
                             method_string](const crow::request &request)
                            {
                                HtaccessDecision htaccess_decision_result;

                                if (s_htaccess().is_enabled())
                                {
                                    std::unordered_map<std::string, std::string>
                                        headers_map;

                                    for (const auto &pair : request.headers)
                                    {
                                        headers_map[pair.first] = pair.second;
                                    }

                                    htaccess_decision_result =
                                        s_htaccess().evaluate(
                                            method_string, request.url,
                                            request.remote_ip_address, headers_map);

                                    if (htaccess_decision_result.action ==
                                        htaccess_action::forbidden)
                                    {
                                        crow::response response(
                                            403,
                                            htaccess_decision_result.body.empty()
                                                ? "{\"error\":\"Access Forbidden\"}"
                                                : htaccess_decision_result.body);

                                        for (const auto &[header_key,
                                                          header_value] :
                                             htaccess_decision_result.headers)
                                        {
                                            response.add_header(header_key,
                                                                header_value);
                                        }

                                        return response;
                                    }

                                    if (htaccess_decision_result.action ==
                                        htaccess_action::redirect)
                                    {
                                        crow::response response(
                                            htaccess_decision_result.status_code,
                                            htaccess_decision_result.body);

                                        for (const auto &[header_key,
                                                          header_value] :
                                             htaccess_decision_result.headers)
                                        {
                                            response.add_header(header_key,
                                                                header_value);
                                        }

                                        return response;
                                    }

                                    if (htaccess_decision_result.action ==
                                        htaccess_action::gone)
                                    {
                                        crow::response response(
                                            410,
                                            htaccess_decision_result.body.empty()
                                                ? "{\"error\":\"Resource Gone\"}"
                                                : htaccess_decision_result.body);

                                        for (const auto &[header_key,
                                                          header_value] :
                                             htaccess_decision_result.headers)
                                        {
                                            response.add_header(header_key,
                                                                header_value);
                                        }

                                        return response;
                                    }
                                }

                                if (cache_time_to_live_seconds > 0 &&
                                    request.method == "GET"_method)
                                {
                                    std::string cache_key =
                                        "GET:" + (request.raw_url.empty()
                                                      ? request.url
                                                      : request.raw_url);
                                    CachedResponse cached_response_data;

                                    if (s_cache_engine().get(cache_key,
                                                             cached_response_data))
                                    {
                                        crow::response response(
                                            cached_response_data.status_code,
                                            cached_response_data.body);

                                        response.add_header(
                                            "Content-Type",
                                            cached_response_data.content_type);
                                        response.add_header("X-Cache", "HIT");

                                        for (const auto &[header_key,
                                                          header_value] :
                                             htaccess_decision_result.headers)
                                        {
                                            response.add_header(header_key,
                                                                header_value);
                                        }

                                        return response;
                                    }

                                    auto response =
                                        native_handler_function(request);

                                    if (response.code >= 200 && response.code < 300)
                                    {
                                        CachedResponse to_cache;

                                        to_cache.status_code = response.code;
                                        to_cache.body = response.body;

                                        auto content_type_header =
                                            response.get_header_value(
                                                "Content-Type");

                                        to_cache.content_type =
                                            content_type_header.empty()
                                                ? "application/json"
                                                : content_type_header;
                                        s_cache_engine().set(
                                            cache_key, to_cache,
                                            std::chrono::seconds(
                                                cache_time_to_live_seconds));
                                    }

                                    response.add_header("X-Cache", "MISS");

                                    for (const auto &[header_key, header_value] :
                                         htaccess_decision_result.headers)
                                    {
                                        response.add_header(header_key,
                                                            header_value);
                                    }

                                    return response;
                                }

                                auto response = native_handler_function(request);

                                for (const auto &[header_key, header_value] :
                                     htaccess_decision_result.headers)
                                {
                                    response.add_header(header_key, header_value);
                                }

                                return response;
                            });
                    }
                }

                if (cache_time_to_live_seconds > 0)
                {
                    LOG_INFO("routes",
                             "Mapped " << method_string << " " << path << " -> "
                                       << handler_name << " (cached "
                                       << cache_time_to_live_seconds << "s)");
                }
                else
                {
                    LOG_INFO("routes", "Mapped " << method_string << " " << path
                                                 << " -> " << handler_name);
                }

                route_count_++;
            }
            else if (handler_object.is<sol::protected_function>())
            {
                sol::protected_function lua_handler_function =
                    handler_object.as<sol::protected_function>();

                std::lock_guard<std::mutex> handler_lock(mutex_);

                lua_handlers_.push_back(lua_handler_function);
                size_t handler_index = lua_handlers_.size() - 1;

                route_pattern.lua_handler_index = handler_index;
                rest_routes_.push_back(route_pattern);

                if (!is_hot_reload_)
                {
                    if (route_pattern.has_parameters)
                    {
                        bind_dynamic_crow_route(path, method);
                    }
                    else
                    {
                        server_.app().route_dynamic(path).methods(method)(
                            [this, handler_index, cache_time_to_live_seconds,
                             method_string](const crow::request &request)
                            {
                                HtaccessDecision htaccess_decision_result;
                                std::unordered_map<std::string, std::string>
                                    headers_map;

                                for (const auto &pair : request.headers)
                                {
                                    headers_map[pair.first] = pair.second;
                                }

                                if (s_htaccess().is_enabled())
                                {
                                    htaccess_decision_result =
                                        s_htaccess().evaluate(
                                            method_string, request.url,
                                            request.remote_ip_address,
                                            headers_map);

                                    if (htaccess_decision_result.action ==
                                        htaccess_action::forbidden)
                                    {
                                        crow::response response(
                                            403, htaccess_decision_result.body
                                                         .empty()
                                                     ? "{\"error\":\"Access "
                                                       "Forbidden\"}"
                                                     : htaccess_decision_result
                                                           .body);

                                        for (const auto &[header_key,
                                                          header_value] :
                                             htaccess_decision_result.headers)
                                        {
                                            response.add_header(header_key,
                                                                header_value);
                                        }

                                        return response;
                                    }

                                    if (htaccess_decision_result.action ==
                                        htaccess_action::redirect)
                                    {
                                        crow::response response(
                                            htaccess_decision_result
                                                .status_code,
                                            htaccess_decision_result.body);

                                        for (const auto &[header_key,
                                                          header_value] :
                                             htaccess_decision_result.headers)
                                        {
                                            response.add_header(header_key,
                                                                header_value);
                                        }

                                        return response;
                                    }

                                    if (htaccess_decision_result.action ==
                                        htaccess_action::gone)
                                    {
                                        crow::response response(
                                            410, htaccess_decision_result.body
                                                         .empty()
                                                     ? "{\"error\":\"Resource "
                                                       "Gone\"}"
                                                     : htaccess_decision_result
                                                           .body);

                                        for (const auto &[header_key,
                                                          header_value] :
                                             htaccess_decision_result.headers)
                                        {
                                            response.add_header(header_key,
                                                                header_value);
                                        }

                                        return response;
                                    }
                                }

                                if (cache_time_to_live_seconds > 0 &&
                                    request.method == "GET"_method)
                                {
                                    std::string cache_key =
                                        "GET:" + (request.raw_url.empty()
                                                      ? request.url
                                                      : request.raw_url);
                                    CachedResponse cached_response_data;

                                    if (s_cache_engine().get(
                                            cache_key, cached_response_data))
                                    {
                                        crow::response response(
                                            cached_response_data.status_code,
                                            cached_response_data.body);

                                        response.add_header(
                                            "Content-Type",
                                            cached_response_data.content_type);
                                        response.add_header("X-Cache", "HIT");

                                        for (const auto &[header_key,
                                                          header_value] :
                                             htaccess_decision_result.headers)
                                        {
                                            response.add_header(header_key,
                                                                header_value);
                                        }

                                        return response;
                                    }
                                }

                                std::lock_guard<std::mutex> route_lock(
                                    lua_.mutex());

                                sol::table request_table =
                                    lua_.state().create_table();
                                std::unordered_map<std::string, std::string>
                                    empty_parameters;

                                populate_lua_request(
                                    lua_, request, empty_parameters,
                                    htaccess_decision_result, request_table);

                                auto result =
                                    lua_handlers_[handler_index](request_table);

                                if (!result.valid())
                                {
                                    sol::error lua_error = result;

                                    LOG_ERROR("routes",
                                              "Lua route error: "
                                                  << lua_error.what());

                                    return crow::response(
                                        500, "{\"error\":\"Internal "
                                             "Lua handler error\"}");
                                }

                                sol::table response_table =
                                    result.get<sol::table>();
                                int status =
                                    response_table.get_or("status", 200);
                                std::string body =
                                    response_table.get_or<std::string>("body",
                                                                       "");
                                std::string content_type =
                                    response_table.get_or<std::string>(
                                        "content_type", "application/json");

                                if (cache_time_to_live_seconds > 0 &&
                                    status >= 200 && status < 300)
                                {
                                    std::string cache_key =
                                        "GET:" + (request.raw_url.empty()
                                                      ? request.url
                                                      : request.raw_url);
                                    CachedResponse to_cache;

                                    to_cache.status_code = status;
                                    to_cache.body = body;
                                    to_cache.content_type = content_type;

                                    s_cache_engine().set(
                                        cache_key, to_cache,
                                        std::chrono::seconds(
                                            cache_time_to_live_seconds));
                                }

                                crow::response response(status, body);

                                response.add_header("Content-Type",
                                                    content_type);

                                if (cache_time_to_live_seconds > 0)
                                {
                                    response.add_header("X-Cache", "MISS");
                                }

                                for (const auto &[header_key, header_value] :
                                     htaccess_decision_result.headers)
                                {
                                    response.add_header(header_key,
                                                        header_value);
                                }

                                sol::optional<sol::table> user_headers =
                                    response_table["headers"];

                                if (user_headers)
                                {
                                    user_headers.value().for_each(
                                        [&response](sol::object key,
                                                    sol::object value)
                                        {
                                            if (key.is<std::string>() &&
                                                value.is<std::string>())
                                            {
                                                response.add_header(
                                                    key.as<std::string>(),
                                                    value.as<std::string>());
                                            }
                                        });
                                }

                                return response;
                            });
                    }
                }

                if (cache_time_to_live_seconds > 0)
                {
                    LOG_INFO("routes", "Mapped " << method_string << " " << path
                                                 << " -> [Lua] (cached "
                                                 << cache_time_to_live_seconds
                                                 << "s)");
                }
                else
                {
                    LOG_INFO("routes", "Mapped " << method_string << " " << path
                                                 << " -> [Lua]");
                }

                route_count_++;
            }
            else
            {
                LOG_ERROR("routes", "Invalid handler type for route: " << path);
            }
        });

    binding_registered_ = true;
}

crow::response router::dispatch_request(const crow::request &request)
{
    if (!routes_file_path_.empty())
    {
        std::error_code error_code;
        auto current_modification_time =
            std::filesystem::last_write_time(routes_file_path_, error_code);

        if (!error_code &&
            current_modification_time > last_routes_modification_time_)
        {
            last_routes_modification_time_ = current_modification_time;
            reload_routes(routes_file_path_);
        }
    }

    std::string clean_path = request.url;
    auto query_position = clean_path.find('?');

    if (query_position != std::string::npos)
    {
        clean_path = clean_path.substr(0, query_position);
    }

    HtaccessDecision htaccess_decision_result;
    std::unordered_map<std::string, std::string> headers_map;

    for (const auto &pair : request.headers)
    {
        headers_map[pair.first] = pair.second;
    }

    if (s_htaccess().is_enabled())
    {
        htaccess_decision_result =
            s_htaccess().evaluate(crow::method_name(request.method), clean_path,
                                  request.remote_ip_address, headers_map);

        if (htaccess_decision_result.action == htaccess_action::forbidden)
        {
            crow::response response(
                403, htaccess_decision_result.body.empty()
                         ? "{\"error\":\"Access Forbidden\",\"status\":403}"
                         : htaccess_decision_result.body);

            response.add_header("Content-Type", "application/json");

            for (const auto &[header_key, header_value] :
                 htaccess_decision_result.headers)
            {
                response.add_header(header_key, header_value);
            }

            return response;
        }

        if (htaccess_decision_result.action == htaccess_action::redirect)
        {
            crow::response response(htaccess_decision_result.status_code,
                                    htaccess_decision_result.body);

            for (const auto &[header_key, header_value] :
                 htaccess_decision_result.headers)
            {
                response.add_header(header_key, header_value);
            }

            return response;
        }

        if (htaccess_decision_result.action == htaccess_action::gone)
        {
            crow::response response(
                410, htaccess_decision_result.body.empty()
                         ? "{\"error\":\"Resource Gone\",\"status\":410}"
                         : htaccess_decision_result.body);

            response.add_header("Content-Type", "application/json");

            for (const auto &[header_key, header_value] :
                 htaccess_decision_result.headers)
            {
                response.add_header(header_key, header_value);
            }

            return response;
        }

        if (htaccess_decision_result.action == htaccess_action::rewrite &&
            !htaccess_decision_result.rewritten_url.empty())
        {
            clean_path = htaccess_decision_result.rewritten_url;
            query_position = clean_path.find('?');

            if (query_position != std::string::npos)
            {
                clean_path = clean_path.substr(0, query_position);
            }
        }
    }

    std::vector<std::string> request_segments = split_path(clean_path);
    std::unordered_map<std::string, std::string> extracted_parameters;
    const RestRoutePattern *matched_route = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const auto &route_candidate : rest_routes_)
        {
            if (route_candidate.method != request.method)
            {
                continue;
            }

            extracted_parameters.clear();

            if (match_route(route_candidate, request_segments,
                            extracted_parameters))
            {
                matched_route = &route_candidate;
                break;
            }
        }
    }

    if (!matched_route)
    {
        std::string not_found_body = s_htaccess().get_error_document(404);

        if (not_found_body.empty())
        {
            not_found_body =
                "{\"error\":\"Resource not found\",\"status\":404}";
        }

        crow::response response(404, not_found_body);

        response.add_header("Content-Type", "application/json");

        for (const auto &[header_key, header_value] :
             htaccess_decision_result.headers)
        {
            response.add_header(header_key, header_value);
        }

        return response;
    }

    if (matched_route->cache_time_to_live_seconds > 0 && request.method == "GET"_method)
    {
        std::string cache_key =
            "GET:" + (request.raw_url.empty() ? request.url : request.raw_url);
        CachedResponse cached_response_data;

        if (s_cache_engine().get(cache_key, cached_response_data))
        {
            crow::response response(cached_response_data.status_code,
                                    cached_response_data.body);

            response.add_header("Content-Type",
                                cached_response_data.content_type);
            response.add_header("X-Cache", "HIT");

            for (const auto &[header_key, header_value] :
                 htaccess_decision_result.headers)
            {
                response.add_header(header_key, header_value);
            }

            return response;
        }
    }

    crow::response response;

    if (!matched_route->native_handler_name.empty())
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto rest_iterator =
            rest_native_handlers_.find(matched_route->native_handler_name);

        if (rest_iterator != rest_native_handlers_.end())
        {
            response = rest_iterator->second(request, extracted_parameters);
        }
        else
        {
            auto native_iterator =
                native_handlers_.find(matched_route->native_handler_name);

            if (native_iterator != native_handlers_.end())
            {
                response = native_iterator->second(request);
            }
            else
            {
                LOG_ERROR("routes", "Handler '"
                                        << matched_route->native_handler_name
                                        << "' not found during dispatch");

                return crow::response(500, "{\"error\":\"Handler not found\"}");
            }
        }
    }
    else if (matched_route->lua_handler_index < lua_handlers_.size())
    {
        std::lock_guard<std::mutex> route_lock(lua_.mutex());
        sol::table request_table = lua_.state().create_table();

        populate_lua_request(lua_, request, extracted_parameters,
                             htaccess_decision_result, request_table);

        auto result =
            lua_handlers_[matched_route->lua_handler_index](request_table);

        if (!result.valid())
        {
            sol::error lua_error = result;

            LOG_ERROR("routes", "Lua route error: " << lua_error.what());

            return crow::response(500,
                                  "{\"error\":\"Internal Lua handler error\"}");
        }

        sol::table response_table = result.get<sol::table>();
        int status = response_table.get_or("status", 200);
        std::string body = response_table.get_or<std::string>("body", "");
        std::string content_type = response_table.get_or<std::string>(
            "content_type", "application/json");

        response = crow::response(status, body);
        response.add_header("Content-Type", content_type);

        sol::optional<sol::table> user_headers = response_table["headers"];

        if (user_headers)
        {
            user_headers.value().for_each(
                [&response](sol::object key, sol::object value)
                {
                    if (key.is<std::string>() && value.is<std::string>())
                    {
                        response.add_header(key.as<std::string>(),
                                            value.as<std::string>());
                    }
                });
        }
    }

    if (matched_route->cache_time_to_live_seconds > 0 && request.method == "GET"_method &&
        response.code >= 200 &&
        response.code < 300)
    {
        std::string cache_key =
            "GET:" + (request.raw_url.empty() ? request.url : request.raw_url);
        CachedResponse to_cache;

        to_cache.status_code = response.code;
        to_cache.body = response.body;

        auto content_type_header = response.get_header_value("Content-Type");

        to_cache.content_type = content_type_header.empty()
                                    ? "application/json"
                                    : content_type_header;

        s_cache_engine().set(
            cache_key, to_cache,
            std::chrono::seconds(matched_route->cache_time_to_live_seconds));

        response.add_header("X-Cache", "MISS");
    }

    for (const auto &[header_key, header_value] :
         htaccess_decision_result.headers)
    {
        response.add_header(header_key, header_value);
    }

    return response;
}

void router::load_routes(const std::string &route_script_path)
{
    routes_file_path_ = route_script_path;
    std::error_code error_code;
    last_routes_modification_time_ =
        std::filesystem::last_write_time(routes_file_path_, error_code);

    setup_lua_route_binding();

    lua_.load_file(route_script_path);
}

void router::reload_routes(const std::string &route_script_path)
{
    std::lock_guard<std::mutex> lua_lock(lua_.mutex());

    LOG_INFO("routes", "Hot-reloading routes from: " << route_script_path);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        rest_routes_.clear();
        lua_handlers_.clear();
        route_count_ = 0;
        is_hot_reload_ = true;
    }

    try
    {
        lua_.state().script_file(route_script_path);
        std::lock_guard<std::mutex> lock(mutex_);

        LOG_INFO("routes",
                 "Hot-reloaded " << rest_routes_.size() << " route(s)");
    }
    catch (const sol::error &error)
    {
        LOG_ERROR("routes", "Failed to hot-reload routes: " << error.what());
    }

    std::lock_guard<std::mutex> lock(mutex_);
    is_hot_reload_ = false;
}

void router::load_routes_from_dir(const std::string &directory_path)
{
    namespace filesystem = std::filesystem;

    if (!filesystem::exists(directory_path) ||
        !filesystem::is_directory(directory_path))
    {
        return;
    }

    setup_lua_route_binding();

    for (const auto &entry :
         filesystem::recursive_directory_iterator(directory_path))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".lua")
        {
            if (entry.path() == filesystem::path(directory_path) / "routes.lua")
            {
                continue;
            }

            std::string path_string = entry.path().string();

            if (path_string.find("/models/") != std::string::npos ||
                path_string.find("/migrations/") != std::string::npos)
            {
                continue;
            }

            LOG_INFO("routes",
                     "Loading route script: " << entry.path().string());
            lua_.load_file(entry.path().string());
        }
    }
}

size_t router::route_count() const { return route_count_; }

size_t router::handler_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return native_handlers_.size();
}

} // namespace api
