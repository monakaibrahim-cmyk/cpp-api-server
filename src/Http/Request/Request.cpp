#include "Request.h"

Http::Request::Request(const std::string& raw)
{
    std::istringstream stream(raw);
    std::string line;

    if (std::getline(stream, line))
    {
        std::istringstream line_stream(line);
        line_stream >> method >> path >> version;
    }

    while (std::getline(stream, line) && line != "\r")
    {
        size_t colon = line.find(':');

        if (colon != std::string::npos)
        {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 2);
            headers[key] = value;
        }
    }

    std::getline(stream, body, '\0');
}
