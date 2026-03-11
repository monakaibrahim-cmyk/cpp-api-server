#include <syncstream>

#include "Response.h"
#include "Logs/Logs.h"

Http::Response::Response(Status c, std::string b) :
    status(c),
    body(b)
{
    //    
}

std::string Http::Response::render() const
{
    std::string raw = "HTTP/1.1 " + std::to_string((int)status) + " OK\r\n";

    for (auto const& [key, value] : headers)
    {
        raw += key + ": " + value + "\r\n";
    }

    raw += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    raw += "\r\n";
    raw += body;

    LOG_DEBUG("SENT\n" + raw +"\n");
    
    return raw;
}
