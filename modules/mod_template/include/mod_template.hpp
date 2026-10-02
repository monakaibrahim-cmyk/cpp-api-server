#pragma once

#include "api/module.hpp"

namespace api
{

class mod_template : public module
{
public:
    mod_template();
    ~mod_template() override = default;

    void on_config_load(server_config& cfg) override;
    void on_init(const server_config& cfg) override;
    void on_handlers_register(router& rtr) override;
    void on_lua_init(lua_engine& lua) override;
    bool on_request_override(crow::request& req, crow::response& res) override;
    void on_before_request(crow::request& req, crow::response& res) override;
    void on_after_request(crow::request& req, crow::response& res, double duration_ms) override;
    void on_server_startup(api_server& server) override;
    void on_server_shutdown() override;
};

} // namespace api
