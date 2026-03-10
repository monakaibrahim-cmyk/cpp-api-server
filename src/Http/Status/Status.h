#ifndef HTTP_STATUS_H
#define HTTP_STATUS_H

#include <string>
#include <cstdint>

namespace Http
{
    enum class Status : std::int16_t
    {
        CONTINUE = 100,
        SWITCHING_PROTOCOLS = 101,
        PROCESSING = 102,

        OK = 200,
        CREATED = 201,
        ACCEPTED = 202,
        NO_CONTENT = 204,

        MULTIPLE_CHOICES = 300,
        MOVED_PERMANENTLY = 301,
        FOUND = 302,
        SEE_OTHER = 303,
        NOT_MODIFIED = 304,

        BAD_REQUEST = 400,
        UNAUTHORIZED = 401,
        FORBIDDEN = 403,
        NOT_FOUND = 404,
        METHOD_NOT_ALLOWED = 405,
        NOT_ACCEPTABLE = 406,
        CONFLICT = 409,
        GONE = 410,
        PAYLOAD_TOO_LARGE = 413,
        UNSUPPORTED_MEDIA_TYPE = 415,
        TOO_MANY_REQUESTS = 429,

        INTERNAL_SERVER_ERROR = 500,
        NOT_IMPLEMENTED = 501,
        BAD_GATEWAY = 502,
        SERVICE_UNAVAILABLE = 503,
        GATEWAY_TIMEOUT = 504
    };

    std::string code(Status code);
}

#endif // HTTP_STATUS_H