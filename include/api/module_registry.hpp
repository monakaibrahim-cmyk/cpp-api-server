#pragma once

#include "api/script_mgr.hpp"

namespace api
{

using module_registry = script_mgr;

} // namespace api

#define API_REGISTER_MODULE(module_class) API_REGISTER_SCRIPT(module_class)
