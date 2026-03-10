#ifndef HTTP_ROUTER_H
#define HTTP_ROUTER_H

#include <map>
#include <functional>

#include "Http/Request/Request.h"
#include "Http/Response/Response.h"
#include "Http/Status/Status.h"

namespace Http
{
    class Router
    {
        std::string _prefix;
        std::map<std::string, std::function<Http::Response(const Http::Request&)>> routes;

    public:
        Router(std::string prefix = "");
        void add(std::string path, std::function<Http::Response(const Http::Request&)> handler);
        Http::Response handle(const Http::Request& request);
        Http::Response authguard(const Http::Request& request, std::function<Http::Response(const Http::Request&)> next, const std::string& secret);
    };
} // Http

#endif // HTTP_ROUTER_H