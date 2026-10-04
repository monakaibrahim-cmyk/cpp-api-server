#include <mod_jwt.h>
#include <core/config.h>
#include <core/logger.h>
#include <scripting/lua_engine.h>
#include <core/module_registry.h>
#include <server/router.h>
#include <core/service_registry.h>

#include <filesystem>

namespace api
{

mod_jwt::mod_jwt()
    : module("mod_jwt", "1.0.0",
             "High-performance OpenSSL asymmetric token signing and "
             "verification module")
{
}

void mod_jwt::on_config_load(ServerConfig & /*configuration*/)
{
    namespace filesystem = std::filesystem;

    jwt_configuration_.algorithm = "RS256";
    jwt_configuration_.issuer = "api-server";
    jwt_configuration_.default_time_to_live_seconds = 3600;
    jwt_configuration_.clock_tolerance_seconds = 60;

    std::string default_private_key = "config/certs/jwt_private.pem";
    std::string default_public_key = "config/certs/jwt_public.pem";

    if (filesystem::exists(default_private_key))
    {
        jwt_configuration_.private_key_path = default_private_key;
    }

    if (filesystem::exists(default_public_key))
    {
        jwt_configuration_.public_key_path = default_public_key;
    }
}

void mod_jwt::on_init(const ServerConfig & /*configuration*/)
{
    engine_ = std::make_shared<jwt_engine>(jwt_configuration_);

    s_services().register_service<jwt_engine>("jwt", engine_);

    LOG_INFO("jwt",
             "Initialized mod_jwt [Algorithm: "
                 << engine_->get_algorithm() << ", PrivateKey: "
                 << (engine_->has_private_key() ? "LOADED" : "NOT LOADED")
                 << ", PublicKey: "
                 << (engine_->has_public_key() ? "LOADED" : "NOT LOADED")
                 << "]");
}

void mod_jwt::on_lua_init(lua_engine &lua_engine_instance)
{
    if (engine_)
    {
        engine_->bind_lua(lua_engine_instance);
    }
}

void mod_jwt::on_handlers_register(router &server_router)
{
    auto engine_pointer = engine_;

    server_router.register_native_handler(
        "auth.status",
        [engine_pointer](const crow::request & /*request*/)
        {
            if (!engine_pointer)
            {
                return crow::response(
                    503, "{\"error\":\"JWT engine uninitialized\"}");
            }

            crow::json::wvalue status_json;

            status_json["status"] = "ok";
            status_json["algorithm"] = engine_pointer->get_algorithm();
            status_json["issuer"] = engine_pointer->get_issuer();
            status_json["has_private_key"] = engine_pointer->has_private_key();
            status_json["has_public_key"] = engine_pointer->has_public_key();

            crow::response response(200, status_json.dump());

            response.add_header("Content-Type", "application/json");

            return response;
        });

    server_router.register_native_handler(
        "auth.public_key",
        [engine_pointer](const crow::request & /*request*/)
        {
            if (!engine_pointer || !engine_pointer->has_public_key())
            {
                return crow::response(
                    404, "{\"error\":\"No public key configured\"}");
            }

            std::string public_pem = engine_pointer->get_public_key_pem();
            crow::response response(200, public_pem);

            response.add_header("Content-Type", "application/x-pem-file");

            return response;
        });

    server_router.register_native_handler(
        "auth.verify_bearer",
        [engine_pointer](const crow::request &request)
        {
            if (!engine_pointer)
            {
                return crow::response(
                    503, "{\"error\":\"JWT engine uninitialized\"}");
            }

            auto auth_header = request.get_header_value("Authorization");
            std::string token = jwt_engine::extract_bearer_token(auth_header);

            if (token.empty())
            {
                crow::response response(401, "{\"error\":\"Missing or invalid "
                                             "Bearer token\",\"status\":401}");

                response.add_header("Content-Type", "application/json");

                return response;
            }

            auto verification_result = engine_pointer->verify(token);

            if (!verification_result.is_valid)
            {
                crow::json::wvalue error_json;

                error_json["error"] = verification_result.error_message;
                error_json["status"] = 401;

                crow::response response(401, error_json.dump());

                response.add_header("Content-Type", "application/json");

                return response;
            }

            crow::json::wvalue success_json;

            success_json["authenticated"] = true;
            success_json["subject"] = verification_result.subject;
            success_json["issuer"] = verification_result.issuer;
            success_json["expires_at"] = verification_result.expires_at;

            crow::response response(200, success_json.dump());

            response.add_header("Content-Type", "application/json");

            return response;
        });
}

void mod_jwt::on_server_shutdown() { LOG_INFO("jwt", "Shutting down mod_jwt"); }

jwt_engine &mod_jwt::get_engine() { return *engine_; }

} // namespace api

API_REGISTER_MODULE(api::mod_jwt);
