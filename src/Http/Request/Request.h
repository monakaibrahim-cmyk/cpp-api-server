#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>
#include <sstream>
#include <map>

namespace Http
{
    class Request
    {
    public:
        std::string method;
        std::string path;
        std::string version;
        std::map<std::string, std::string> headers;
        std::string body;

        Request(const std::string& raw);
    };
} // Http

#endif // HTTP_REQUEST_H
