#include <thread>
#include <vector>

#include "Server.h"
#include "Logs/Logs.h"

void Http::Server::on_request(std::function<Http::Response(const Http::Request&)> handler)
{
    _handler = handler;
}

void Http::Server::start()
{
    LOG_INFO("Server listening on port " + std::to_string(_port));

    _listen = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;

    setsockopt(_listen, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(_port);

    bind(_listen, (struct sockaddr*)&address, sizeof(address));
    listen(_listen, 128);

    _running = true;

    while (_running)
    {
        sockaddr_in c_address;
        socklen_t c_length = sizeof(c_address);
        int client = accept(_listen, (struct sockaddr*)&c_address, &c_length);

        if (client >= 0)
        {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(c_address.sin_addr), ip, INET_ADDRSTRLEN);

            std::thread([this, client, ip]() {
                char buffer[4096] = {0};
                read(client, buffer, 4096);

                Request request(buffer);

                std::string temp = request.method + " " + request.path + " - " + std::string(ip);
                LOG_INFO(temp);

                Response response = _handler(request);

                std::string raw = response.render();
                send(client, raw.c_str(), raw.length(), 0);

                close(client);
            })
            .detach();
        }
        else
        {
            LOG_ERROR("Failed to accept client connection.");
            continue;
        }
    }

    LOG_INFO("Server Shutdown.");
}

void Http::Server::stop()
{
    _running = false;

    if (_listen != -1)
    {
        shutdown(_listen, 2);
        close(_listen);
        _listen = -1;
    }
}
