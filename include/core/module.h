#pragma once

#include <scripting/script_mgr.h>

namespace api
{

/**
 * @brief Base type alias for modular C++ plugins and extensions.
 *
 * @details Developers creating custom C++ extensions subclass @ref api::module
 * (which aliases @ref api::module_script) and register the class using the
 * @ref API_REGISTER_MODULE macro.
 *
 * Example:
 * @code{.cpp}
 * class my_plugin : public api::module
 * {
 * public:
 *     my_plugin() : module("my_plugin", "1.0.0", "Custom plugin") {}
 *     void on_init(const api::ServerConfig& configuration) override {}
 * };
 *
 * API_REGISTER_MODULE(my_plugin);
 * @endcode
 */
using module = module_script;

} // namespace api
