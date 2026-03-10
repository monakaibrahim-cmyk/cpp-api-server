#include <iostream>
#include <syncstream>
#include <cstdlib>
#include <iomanip>

#include "Command/Processor.h"
#include "Json/Builder.h"
#include "Http/Request/Request.h"
#include "Http/Response/Response.h"
#include "Http/Router/Router.h"
#include "Http/Status/Status.h"
#include "Http/Server/Server.h"

int main(int argc, char** argv)
{
    Http::Router router("/api/v1");
    Http::Server app(8080);
    Command::Processor processor;

    processor.add(
        "clear",
        "Clear the terminal",
        [](auto& args) 
        {
            std::cout << "\033[2J\033[1;1H" << std::flush;
            std::osyncstream(std::cout) << "CMD] >>" << std::flush;
        }
    );

    // processor.add(
    //     "start",
    //     "Start the server",
    //     [&app](auto& args)
    //     {
    //         LOG_INFO("Starting Server");
    //         app.start();
    //     }
    // );

    // processor.add(
    //     "stop",
    //     "Gracefully stop the server",
    //     [&app](auto& args)
    //     {
    //         LOG_INFO("Shutdown signal received. Stopping server.");
    //         app.stop();
    //     }
    // );

    processor.add(
        "exit",
        "Exit the Program",
        [&app](auto& args)
        {
            app.stop();
            exit(EXIT_SUCCESS);
        }
    );

    router.add("/status", [&app](auto& request)
    {
        auto now = std::chrono::system_clock::now();
        auto uptime_duration = now - app.time();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(uptime_duration).count();
        auto start_time_t = std::chrono::system_clock::to_time_t(app.time());
        std::stringstream stream;
        Json::Builder json;

        std::stringstream time;
        time << std::put_time(std::localtime(&start_time_t), "%Y-%m-%d %H:%M:%S");

        json.add("status", "Running");
        json.add("uptime_seconds", (int)seconds);
        json.add("started_at", time.str());

        auto response = Http::Response(Http::Status::OK, json.build());
        response.headers["Content-Type"] = "application/json";

        return response;
    });

    app.on_request([&router](const Http::Request& request)
    {
        return router.handle(request);
    });

    std::thread console([&processor]()
    {
        std::string line;

        while (true)
        {
            if (!std::getline(std::cin, line))
            {
                break;
            }

            std::cout << "\033[1A\033[2K";

            if (line.empty())
            {
                continue;
            }

            processor.execute(line);
        }
    });

    console.detach();
    app.start();

    return EXIT_SUCCESS;
}
