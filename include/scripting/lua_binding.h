#pragma once

#include <sol/sol.hpp>
#include <string>

namespace api
{

/**
 * @brief Retrieves or creates a top-level global module namespace table in the
 * Lua state.
 *
 * @details Used by modular extension plugins to register types, functions, and
 * state without polluting the global Lua scope.
 *
 * Example C++ usage:
 * @code{.cpp}
 * sol::state& state = lua_engine_instance.state();
 * sol::table mod_ns = api::lua_get_or_create_namespace(state, "mod_auth");
 *
 * // Bind class, variable, and function
 * mod_ns.new_usertype<UserSession>("UserSession", "token",
 * &UserSession::token); mod_ns["version"] = "1.0.0"; mod_ns["hash_password"] =
 * &hash_password_fn;
 * @endcode
 *
 * Example Lua consumption:
 * @code{.lua}
 * local session = mod_auth.UserSession.new()
 * print("Auth module version:", mod_auth.version)
 * @endcode
 *
 * @param[in,out] lua_state Reference to the active Sol2 Lua state.
 * @param[in] namespace_name Unique identifier string for the module namespace
 * table.
 * @return sol::table Sol2 table representing the top-level namespace.
 */
inline sol::table lua_get_or_create_namespace(sol::state &lua_state,
                                              const std::string &namespace_name)
{
    sol::table namespace_table =
        lua_state[namespace_name].get_or_create<sol::table>();

    return namespace_table;
}

/**
 * @brief Retrieves or creates a nested sub-namespace table within an existing
 * Lua table.
 *
 * @details Facilitates hierarchical multi-level package structures (e.g. @c
 * mod_auth.crypto).
 *
 * Example C++ usage:
 * @code{.cpp}
 * sol::table auth_ns = api::lua_get_or_create_namespace(state, "mod_auth");
 * sol::table crypto_ns = api::lua_get_or_create_sub_namespace(auth_ns,
 * "crypto"); crypto_ns["sha256"] = &sha256_fn;
 * @endcode
 *
 * Example Lua consumption:
 * @code{.lua}
 * local digest = mod_auth.crypto.sha256("password")
 * @endcode
 *
 * @param[in,out] parent_table Enclosing parent Sol2 table.
 * @param[in] namespace_name Name of the sub-namespace table to create or fetch.
 * @return sol::table Sol2 table representing the nested sub-namespace.
 */
inline sol::table
lua_get_or_create_sub_namespace(sol::table &parent_table,
                                const std::string &namespace_name)
{
    sol::table sub_namespace_table =
        parent_table[namespace_name].get_or_create<sol::table>();

    return sub_namespace_table;
}

} // namespace api
