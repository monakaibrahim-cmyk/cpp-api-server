#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <chrono>
#include <functional>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "Http/Request/Request.h"
#include "Http/Response/Response.h"

namespace Http
{
    class Server
    {
    private:
        int _port;
        int _listen;
        std::atomic<bool> _running{ false };
        std::function<Http::Response(const Http::Request&)> _handler;
        std::chrono::system_clock::time_point _time;

    public:
        Server(int port) :
            _port(port),
            _listen(-1)
        {
            _time = std::chrono::system_clock::now();
        }

        void on_request(std::function<Http::Response(const Http::Request&)> handler);
        void start();
        void stop();

        std::chrono::system_clock::time_point time() const
        {
            return _time;
        }
    };
} // Http


#endif // HTTP_SERVER_H