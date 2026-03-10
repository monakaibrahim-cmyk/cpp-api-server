#include "Status.h"

std::string Http::code(Status code)
{
    switch(code)
    {
    case Status::OK:
        return "OK";
    case Status::CREATED:
        return "Created";
    case Status::BAD_REQUEST:
        return "Bad Request";
    case Status::UNAUTHORIZED:
        return "Unauthorized";
    case Status::NOT_FOUND:
        return "Not Found";
    case Status::INTERNAL_SERVER_ERROR:
        return "Internal Server Error";
    default:
        return "Unknown";
    }
}
