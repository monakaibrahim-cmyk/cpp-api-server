#include "Router.h"

Http::Router::Router(std::string prefix) :
    _prefix(prefix)
{
    //
}

void Http::Router::add(std::string path, std::function<Http::Response(const Http::Request&)> handler)
{
    routes[_prefix + path] = handler;
}

Http::Response Http::Router::handle(const Http::Request& request)
{
    if (request.path == "/")
    {
        return Http::Response(Http::Status::FORBIDDEN, "403 Forbidden");
    }

    if (routes.count(request.path))
    {
        return routes[request.path](request);
    }

    return Http::Response(Http::Status::NOT_FOUND, "404 Not Found");
}

Http::Response Http::Router::authguard(const Http::Request& request, std::function<Http::Response(const Http::Request&)> next, const std::string& secret)
{
    auto it = request.headers.find("Authorization");

    if (it == request.headers.end() || it->second != ("Bearer " + secret))
    {
        return Http::Response(Http::Status::UNAUTHORIZED, "401 Unauthorized");
    }

    return next(request);
}
