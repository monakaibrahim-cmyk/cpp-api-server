#include "api/server.hpp"
#include "api/logger.hpp"

namespace api
{

api_server::api_server(const server_config& cfg)
    : config_(cfg)
{
}

api_server::~api_server()
{
    stop();
}

void api_server::start()
{
    if (running_.load())
    {
        return;
    }

    running_.store(true);

    app_.signal_clear();
    app_.port(config_.port).concurrency(config_.threads);
    app_.loglevel(crow::LogLevel::Warning);

    server_thread_ = std::thread(
        [this]()
        {
            app_.run();
        }
    );
}

void api_server::stop()
{
    if (!running_.load())
    {
        return;
    }

    app_.stop();

    if (server_thread_.joinable())
    {
        server_thread_.join();
    }

    running_.store(false);
}

bool api_server::is_running() const
{
    return running_.load();
}

api_app_t& api_server::app()
{
    return app_;
}

} // namespace api
