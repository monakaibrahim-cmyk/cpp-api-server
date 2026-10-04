#pragma once

#include <scripting/script_mgr.h>

namespace api
{

/**
 * @brief Registry alias for inspecting and discovering registered dynamic
 * modules.
 *
 * @details Directs all module lookups and queries through the central @ref
 * script_mgr singleton.
 */
using module_registry = script_mgr;

} // namespace api

/**
 * @brief Macro for self-registering an API module plugin at static
 * initialization time.
 *
 * @details Places a static registrar struct in an anonymous namespace that
 * automatically registers the module instance with the global @ref
 * api::script_mgr before @c main() begins.
 *
 * Example:
 * @code{.cpp}
 * API_REGISTER_MODULE(api::mod_orm);
 * @endcode
 */
#define API_REGISTER_MODULE(module_class) API_REGISTER_SCRIPT(module_class)
