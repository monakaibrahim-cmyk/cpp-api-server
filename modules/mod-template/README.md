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
│   └── mod_your_name.h        # Script class declarations
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

4. Expose Module Types to Lua in `on_lua_init`:
   ```cpp
   void mod_your_name::on_lua_init(lua_engine& lua)
   {
       std::lock_guard<std::mutex> lock(lua.mutex());
       auto& state = lua.state();

       // Struct UserType (data fields + constructor)
       state.new_usertype<my_struct>(
           "my_struct",
           sol::constructors<my_struct()>(),
           "id", &my_struct::id,
           "val", &my_struct::val
       );

       // Class UserType (methods + properties)
       state.new_usertype<my_class>(
           "my_class",
           sol::constructors<my_class()>(),
           "name", sol::property(&my_class::name, &my_class::set_name),
           "process", &my_class::process
       );

       // Namespace Table
       sol::table ns = lua_get_or_create_namespace(state, "mod_your_name");
       ns["my_struct"] = state["my_struct"];
       ns["my_class"] = state["my_class"];
       ns["version"] = "1.0.0";
       ns["active_instance"] = &my_instance_; // Variable binding (live C++ pointer)
       ns["compute"] = [](int a, int b) { return a + b; }; // Function binding
   }
   ```

5. Accessing in Lua scripts:
   ```lua
   -- Access namespace and variables
   print(mod_your_name.version)

   -- Call standalone module functions
   local res = mod_your_name.compute(5, 10)

   -- Instantiate and use struct
   local item = mod_your_name.my_struct.new()
   item.id = "user_42"

   -- Instantiate and use class
   local service = mod_your_name.my_class.new()
   service:process(item)

   -- Access live C++ class instance directly
   mod_your_name.active_instance:process(item)
   ```

6. Using the C++ Query Builder in other Modules:
   Other C++ modules can directly include `<api/orm.h>` and use the fluent query builder in C++:
   ```cpp
   #include "api/orm.h"

   void my_service::load_active_users()
   {
       // Fluent C++ query building
       auto result = api::s_orm().table("users")
           .select({"id", "username", "balance"})
           .where("balance", ">=", 100.0)
           .order_by("username", "ASC")
           .limit(10)
           .get();

       for (const auto& row : result.rows)
       {
           std::string name = row.get_string("username");
           double bal = row.get_double("balance");
           // ...
       }

       // Mutations in C++
       api::s_orm().table("logs")
           .insert({
               {"action", "sync"},
               {"status", "completed"}
           });
   }
   ```

7. Recompile:
   ```bash
   cmake --build build -j$(nproc)
   ```
   CMake automatically discovers and compiles any module directory inside `modules/`.
