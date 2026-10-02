# Module Template

This directory serves as the template for creating new modules for the API server.

## Philosophy

- **Modules** provide custom features and services (e.g., database connections, cache backends, authentication providers, cryptography) and hooks to override or extend preexisting behavior.
- **Routing** is handled exclusively in Lua scripts, keeping URL endpoints decoupled and user-configurable without recompiling C++ source.

## Structure

```
mod_your_name/
├── CMakeLists.txt              # Module build script
├── conf/
│   └── mod_your_name.lua.dist  # Configuration template
├── include/
│   └── mod_your_name.hpp       # Script class declarations
└── src/
    └── mod_your_name.cpp       # Implementation & hooks
```

## How to Create a New Module

1. Run the module generator:
   ```bash
   ./modules/create_module.sh mod_database
   ```
   Or copy `modules/mod_template` to `modules/mod_database` and rename classes.

2. Implement hooks in your module:
   - `on_init`: Initialize database connection pools, third-party libraries, or module services
   - `on_lua_init`: Export database client or feature functions to Lua (`db.query`, `db.execute`, etc.)
   - `on_before_request`: Intercept or override request processing (authentication, header injection)
   - `on_server_startup`: Post-startup tasks
   - `on_server_shutdown`: Clean shutdown tasks (close connections, flush buffers)

3. Register with `API_REGISTER_SCRIPT(api::mod_your_name)` at the bottom of your `.cpp` file.

4. Recompile:
   ```bash
   cmake --build build -j$(nproc)
   ```
   CMake automatically discovers and compiles any module directory inside `modules/`.
