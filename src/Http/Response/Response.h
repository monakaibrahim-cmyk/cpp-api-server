#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <string>
#include <map>

#include "Http/Status/Status.h"

namespace Http
{
    class Response
    {
    public:
        Http::Status status;
        std::map<std::string, std::string> headers;
        std::string body;

        Response(Status c, std::string b);
        std::string render() const;
    };
} // Http

#endif // HTTP_RESPONSE_H