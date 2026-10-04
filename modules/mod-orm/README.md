# mod_orm - Pluggable Eloquent Scaffold ORM Module

`mod_orm` is a C++ module providing a pluggable **Scaffold ORM and Eloquent Query Builder** for the server. It is designed as an open template scaffold rather than being hardcoded to SQLite. Users can override and plug in any database driver of their choice (e.g. MySQL, PostgreSQL, ClickHouse, or custom in-memory stores) in C++, while exposing a Laravel-style Eloquent ORM, Query Builder, Active Record Model, and Schema Migration Blueprint interface in Lua.

---

## Architecture Overview

```
                      ┌──────────────────────────────────────────┐
                      │                Lua Scripts               │
                      │  Model.extend()  •  DB.table()  • Schema │
                      └────────────────────┬─────────────────────┘
                                           │
                                           ▼
                      ┌──────────────────────────────────────────┐
                      │         Eloquent ORM Subsystem           │
                      │  Builder • Active Record • Blueprint     │
                      └────────────────────┬─────────────────────┘
                                           │
                                           ▼
                      ┌──────────────────────────────────────────┐
                      │          api::orm_engine (Core)          │
                      │   Registry & Active Driver Dispatch      │
                      └────────────────────┬─────────────────────┘
                                           │
                      ┌────────────────────┼────────────────────┐
                      ▼                    ▼                    ▼
            ┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐
            │   mysql_driver   │ │ postgres_driver  │ │  sqlite_driver   │
            │(Custom C++ Module│ │(Custom C++ Module│ │(Custom C++ Module│
            └──────────────────┘ └──────────────────┘ └──────────────────┘
```

1. **`api::db_driver`**: Pure virtual C++ interface (`connect`, `disconnect`, `execute`, `query`, `scaffold_table`, `table_exists`).
2. **`api::orm_engine`**: Thread-safe driver factory registry and execution coordinator.
3. **Pluggable Architecture**: Completely database-agnostic. If no driver is registered, `null_db_driver` safely handles requests and provides helpful diagnostics without crashing. Developers add their chosen driver module (MySQL, PostgreSQL, SQLite, etc.).
4. **Eloquent Engine**: Laravel-style query builder, active record models, and schema migration blueprints bound automatically during `on_lua_init`.

---

## Overriding the Database Driver in C++

Any user module (such as `mod_mysql` or `mod_postgres`) can register a driver factory and activate it during `on_init`:

### 1. Implement `api::db_driver`

```cpp
#include "api/db_driver.h"

class mysql_driver : public api::db_driver
{
public:
    const std::string& driver_name() const override
    {
        static const std::string s_name = "mysql";
        return s_name;
    }

    bool connect(const std::string& connection_string) override
    {
        // Connect using your client library (e.g. mysqlclient, mariadb-connector-cpp)
        return true;
    }

    void disconnect() override
    {
        // Close connection
    }

    bool is_connected() const override
    {
        return true;
    }

    api::db_result execute(
        const std::string& sql,
        const std::vector<api::db_value>& params = {}) override
    {
        api::db_result res;
        // Execute SQL with parameters
        return res;
    }

    api::db_result query(
        const std::string& sql,
        const std::vector<api::db_value>& params = {}) override
    {
        api::db_result res;
        // Query SQL and populate res.rows
        return res;
    }

    bool table_exists(const std::string& table_name) override
    {
        // Check information_schema or SHOW TABLES
        return true;
    }

    bool scaffold_table(const api::table_schema& schema) override
    {
        // Generate and execute CREATE TABLE IF NOT EXISTS
        return true;
    }

    std::string get_last_error() const override
    {
        return "";
    }
};
```

### 2. Register & Activate in Module

```cpp
#include "api/module.h"
#include "api/module_registry.h"
#include "api/orm.h"

class mod_mysql : public api::module
{
public:
    mod_mysql() : module("mod_mysql", "1.0.0", "MySQL database driver override") {}

    void on_init(const api::server_config& cfg) override
    {
        // Register driver factory
        api::s_orm().register_driver_factory(
            "mysql",
            []() -> std::unique_ptr<api::db_driver>
            {
                return std::make_unique<mysql_driver>();
            }
        );

        // If configured as active driver, connect and set active
        if (cfg.db_driver == "mysql")
        {
            api::s_orm().set_active_driver("mysql", cfg.db_connection);
        }
    }
};

API_REGISTER_MODULE(mod_mysql)
```

---

## Configuration (`config/server.lua`)

```lua
config = {
    -- Database configuration (Scaffold ORM)
    db = {
        driver = "mysql",                         -- e.g. "mysql", "postgres", or "" (unconfigured)
        connection = "host=127.0.0.1;user=root;", -- Connection string or file path
    },
}
```

---

## Eloquent Schema & Migrations (`Schema`)

Create, inspect, and drop tables using Laravel-style migration blueprints:

```lua
-- Create table with fluent column definitions
Schema.create("users", function(table)
    table:id()                                       -- Auto-incrementing primary key 'id'
    table:string("username", 64):unique():notNull()  -- VARCHAR(64) UNIQUE NOT NULL
    table:string("email", 128):notNull()             -- VARCHAR(128) NOT NULL
    table:decimal("balance", 12, 2):default("0.00")  -- DECIMAL(12,2) DEFAULT '0.00'
    table:boolean("is_active"):default("1")          -- BOOLEAN DEFAULT 1
    table:timestamps()                               -- created_at & updated_at DATETIME
end)

-- Check table existence
if Schema.hasTable("users") then
    log_info("schema", "Table 'users' is ready")
end

-- Drop table
-- Schema.drop("users")
-- Schema.dropIfExists("users")
```

### Available Blueprint Column Types:
- `table:id([name])` / `table:increments([name])`
- `table:bigIncrements([name])`
- `table:string(name, [length])`
- `table:text(name)`
- `table:integer(name)`
- `table:bigInteger(name)`
- `table:boolean(name)`
- `table:real(name)` / `table:float(name)`
- `table:decimal(name, [precision], [scale])`
- `table:datetime(name)` / `table:timestamp(name)`
- `table:timestamps()` (creates `created_at` and `updated_at`)
- `table:blob(name)`

### Column Modifiers:
- `:nullable()`
- `:notNull()`
- `:unique()`
- `:default(value)`
- `:primary()`

---

## Eloquent Query Builder (`DB.table`)

Fluent, chainable query builder supporting complex clauses, joins, and aggregates:

### 1. Selecting Data
```lua
-- Select specific columns
local users = DB.table("users")
    :select("id", "username", "email")
    :where("is_active", 1)
    :get()

-- Distinct selection
local domains = DB.table("users")
    :distinct()
    :select("email")
    :get()
```

### 2. Where Clauses
```lua
-- Comparison operators
DB.table("users"):where("balance", ">=", 100):get()

-- Key-value table syntax
DB.table("users"):where({ is_active = 1, role = "admin" }):get()

-- OR conditions
DB.table("users"):where("role", "admin"):orWhere("role", "moderator"):get()

-- IN / NOT IN
DB.table("users"):whereIn("id", { 1, 2, 3, 4 }):get()
DB.table("users"):whereNotIn("status", { "banned", "deleted" }):get()

-- NULL / NOT NULL
DB.table("users"):whereNull("deleted_at"):get()
DB.table("users"):whereNotNull("email_verified_at"):get()

-- BETWEEN
DB.table("users"):whereBetween("balance", 100, 500):get()

-- LIKE
DB.table("users"):whereLike("username", "admin%"):get()
```

### 3. Joins
```lua
local orders = DB.table("orders")
    :join("users", "orders.user_id", "=", "users.id")
    :leftJoin("discounts", "orders.discount_id", "=", "discounts.id")
    :select("orders.id", "users.username", "orders.total")
    :get()
```

### 4. Ordering, Grouping & Pagination
```lua
-- Ordering
DB.table("users"):orderBy("username", "ASC"):get()
DB.table("users"):latest("created_at"):get()
DB.table("users"):oldest():get()

-- Grouping & Having
DB.table("orders")
    :select("user_id", "COUNT(*) as count")
    :groupBy("user_id")
    :having("count > ?", 5)
    :get()

-- Limit & Offset
DB.table("users"):limit(10):offset(20):get()

-- Laravel-style Pagination
local page = DB.table("users"):where("is_active", 1):paginate(1, 15)
-- Returns:
-- {
--     current_page = 1,
--     per_page = 15,
--     total = 42,
--     last_page = 3,
--     data = { ... }
-- }
```

### 5. Aggregates & Value Retrieval
```lua
local total_users = DB.table("users"):count()
local max_balance = DB.table("users"):max("balance")
local min_balance = DB.table("users"):min("balance")
local avg_balance = DB.table("users"):avg("balance")
local sum_balance = DB.table("users"):sum("balance")

-- Check existence
if DB.table("users"):where("username", "john"):exists() then
    log_info("user", "User exists!")
end

-- Pluck single column into an array
local emails = DB.table("users"):pluck("email")

-- Get single value
local balance = DB.table("users"):where("username", "john"):value("balance")
```

### 6. Inserting, Updating & Deleting
```lua
-- Insert
local res = DB.table("users"):insert({
    username = "john",
    email = "john@doe.org",
    balance = 250.00
})
local new_id = DB.table("users"):insertGetId({
    username = "jane",
    email = "jane@doe.org"
})

-- Update
DB.table("users"):where("username", "jane"):update({ balance = 300.00 })

-- Increment / Decrement
DB.table("users"):where("id", 1):increment("login_count", 1)
DB.table("users"):where("id", 1):decrement("credits", 10)

-- Update or Insert (Upsert)
DB.table("settings"):updateOrInsert({ key = "site_name" }, { value = "WEBSITE" })

-- Delete
DB.table("users"):where("balance", "<", 0):delete()

-- Truncate
-- DB.table("sessions"):truncate()
```

### 7. SQL Inspection
```lua
local query = DB.table("users"):where("balance", ">", 50):orderBy("username")
local sql = query:toSql()             -- "SELECT * FROM users WHERE balance > ? ORDER BY username ASC"
local bindings = query:getBindings() -- { 50 }
```

---

## Active Record Models (`Model`)

Define Eloquent models with attribute tracking, dirty checking, automated timestamps, and fluent query chaining:

### 1. Defining a Model

```lua
-- Define User model
User = Model.extend("users", {
    primary_key = "id",
    timestamps = true -- Automatically updates created_at and updated_at
})
```

### 2. Creating & Saving Records

```lua
-- Instant creation
local user = User.create({
    username = "john",
    email = "john@doe.org",
    balance = 999.99
})

-- Instantiation and save
local player = User({
    username = "jane",
    email = "jane@doe.org"
})
player:save() -- Inserts record and sets player.id to last_insert_id
```

### 3. Finding & Querying Records

```lua
-- Find by primary key
local user = User.find(1)
if user then
    log_info("user", "Found: " .. user.username .. " with email " .. user.email)
end

-- Find or throw error
local admin = User.findOrFail(1)

-- Retrieve all records as Model instances
local all_users = User.all()
for _, u in ipairs(all_users) do
    log_info("user", u.username .. " | " .. tostring(u.balance))
end

-- Query builder methods directly accessible on Model class:
local rich_users = User.where("balance", ">=", 500)
    :orderBy("balance", "DESC")
    :limit(5)
    :get() -- Returns array of User model instances
```

### 4. Updating & Dirty Tracking

```lua
local user = User.find(1)
user.balance = user.balance + 100.00

if user:isDirty() then
    log_info("user", "User has unsaved modifications")
    local dirty_fields = user:getDirty() -- { balance = ... }
    user:save() -- Only updates dirty fields!
end
```

### 5. Deleting Records

```lua
-- Delete instance
local user = User.find(10)
if user then
    user:delete()
end

-- Delete by primary key
User.destroy(10)
```

### 6. Serialization

```lua
local user = User.find(1)
local data = user:toArray() -- Lua table { id = 1, username = "...", ... }
local json = user:toJson()  -- JSON string '{"id":1,"username":"...","balance":...}'
print(tostring(user))       -- Automatically calls toJson()
```

---

## Raw Queries (`DB.execute` & `DB.query`)

Parameterized raw queries to execute custom or non-standard SQL statements:

```lua
-- Parameterized Execute (INSERT / UPDATE / DELETE / DDL)
local res = DB.execute("INSERT INTO users (username, email) VALUES (?, ?)", { "kael", "kael@sunstrider.org" })
-- res.success (boolean)
-- res.affected_rows (number)
-- res.last_insert_id (number)

-- Parameterized Query (SELECT)
local rows = DB.query("SELECT * FROM users WHERE balance >= ?", { 100 })
for _, row in ipairs(rows) do
    log_info("user", row.username .. " | " .. tostring(row.balance))
end

-- Single Row Query
local row = DB.query_row("SELECT * FROM users WHERE id = ?", { 1 })
```

---

## Introspection

```lua
local is_conn = DB.is_connected()
local current_driver = DB.driver_name()
local available_drivers = DB.drivers() -- Array of registered driver names
```

---

## Usage in Other C++ Modules (C++ Query Builder)

The ORM engine is not limited to Lua scripting; other C++ modules can use the C++ fluent `query_builder` directly via `api::s_orm().table(...)` by including `<api/orm.h>`:

```cpp
#include "api/orm.h"

void user_service::load_and_update()
{
    // 1. Fluent Select Query
    auto res = api::s_orm().table("users")
        .select({"id", "username", "email", "balance"})
        .where("balance", ">=", 100.0)
        .order_by("username", "ASC")
        .limit(10)
        .get();

    if (res.success)
    {
        for (const auto& row : res.rows)
        {
            std::string user = row.get_string("username");
            double balance = row.get_double("balance");
            // ...
        }
    }

    // 2. First matching row
    auto user_opt = api::s_orm().table("users")
        .where("id", "=", 1)
        .first();

    if (user_opt.has_value())
    {
        std::string email = user_opt->get_string("email");
    }

    // 3. Count & Exists
    int64_t total = api::s_orm().table("users").count();
    bool exists = api::s_orm().table("users").where("username", "arthas").exists();

    // 4. Mutations (Insert, Update, Delete)
    int64_t new_id = api::s_orm().table("users").insert_get_id({
        {"username", "sylvanas"},
        {"email", "sylvanas@forsaken.org"},
        {"balance", 500.0}
    });

    api::s_orm().table("users")
        .where("id", "=", new_id)
        .update({
            {"balance", 750.0}
        });

    api::s_orm().table("users")
        .where("id", "=", new_id)
        .del();
}
```

---

## Utilization of .htaccess Scripting in Lua

The server includes native `.htaccess` policy evaluation and URL rewriting integrated directly into Lua scripting via `htaccess`:

### 1. Loading Configuration Files

Load standard `.htaccess` configuration files:

```lua
-- In scripts/routes.lua or any Lua script
htaccess.load_file("config/.htaccess")
```

### 2. Parsing .htaccess Directives from Strings

```lua
local ht = htaccess.parse([[
    RewriteEngine On

    # IP Access Control
    Order allow,deny
    Deny from 10.0.0.99
    Allow from all

    # Header Manipulation
    Header set X-Content-Type-Options "nosniff"
    Header set X-Frame-Options "DENY"

    # URL Redirection
    Redirect 301 /api/v0/health /api/health

    # Rewrite Rules with Conditions
    RewriteCond %{REQUEST_METHOD} POST
    RewriteRule ^/api/v1/legacy$ /api/v1/modern [R=301,L]

    # Blocked paths with 403 Forbidden
    RewriteRule ^/internal/.*$ - [F]

    # Custom Error Documents
    ErrorDocument 403 {"error":"Access Forbidden by Policy"}
]])
```

### 3. Evaluating Requests in Lua

```lua
-- In custom route handlers or middleware:
local decision = htaccess.apply(req)
-- decision.action: "pass", "rewrite", "redirect", "forbidden", "gone", "custom"
-- decision.status: HTTP status code (200, 301, 302, 403, 404, 410, etc.)
-- decision.url: Rewritten target URL
-- decision.headers: Lua table of response headers (Location, X-Frame-Options, etc.)
-- decision.body: Custom error document or response body

if decision.action == "forbidden" then
    return { status = 403, body = decision.body, headers = decision.headers }
elseif decision.action == "redirect" then
    return { status = decision.status, headers = decision.headers }
end
```

### 4. Programmatic Rule Building in Lua

```lua
local ht = htaccess.new()
ht:set_order("allow,deny")
ht:deny("192.168.1.50")
ht:add_header("set", "X-Custom-Engine", "API-Server")
ht:add_rule("^/legacy/(.*)$", "/api/v1/$1", { redirect = 301, last = true })
```

---

## Eloquent Scaffolding & Artisan Generator

Like Laravel / PHP Eloquent, `mod_orm` provides built-in automated code generation and reverse database scaffolding to generate new Lua Models and Migrations.

### 1. CLI Commands (`API-cli`)

| Command | Description | Example |
|---|---|---|
| `--make:model <Name>` | Generate a new Lua active-record Model | `./build/API-cli --make:model User` |
| `--make:model <Name> --table <tbl>` | Generate model with explicit table name | `./build/API-cli --make:model User --table users` |
| `--make:migration <Name>` | Generate a new Lua migration blueprint | `./build/API-cli --make:migration create_users_table` |
| `--db:tables` | List all discovered tables in active database | `./build/API-cli --db:tables` |
| `--db:scaffold <table_name>` | Reverse-engineer a table into Model & Migration | `./build/API-cli --db:scaffold users` |
| `--db:scaffold all` | Reverse-engineer ALL tables in the database | `./build/API-cli --db:scaffold all` |
| `--models-dir <dir>` | Custom output directory for models | `--models-dir app/models` |
| `--migrations-dir <dir>` | Custom output directory for migrations | `--migrations-dir app/migrations` |

### 2. Programmatic Scaffolding via Lua (`Artisan` & `Schema`)

The `Artisan` facade is available globally (`Artisan`) and under `DB.Artisan`:

```lua
-- Discover all database tables
local tables = Artisan.tables()
for _, tbl in ipairs(tables) do
    print("Found table: " .. tbl)
end

-- Inspect table columns and types
local columns = Artisan.describe("users")
for _, col in ipairs(columns) do
    print(string.format("Col: %s, Type: %s, PK: %s", col.name, col.type, tostring(col.primary)))
end

-- Generate a Model file for a table
Artisan.make_model("users", "scripts/models/user.lua")

-- Generate a Migration file for a table
Artisan.make_migration("users", "scripts/migrations/2026_01_01_000000_create_users_table.lua")

-- Reverse scaffold a single table into both Model and Migration files
Artisan.scaffold("users", "scripts/models", "scripts/migrations")

-- Reverse scaffold ALL tables in the database at once
local count = Artisan.scaffold_all("scripts/models", "scripts/migrations")
print("Total scaffolded tables: " .. count)
```

Scaffolding helpers are also forwarded to `Schema` and `DB`:
- `Schema.tables()` / `DB.tables()`
- `Schema.describe(table_name)` / `DB.describe(table_name)`
- `Schema.make_model(table_name, target_file)`
- `Schema.make_migration(table_name, target_file)`
- `Schema.scaffold_all(models_dir, migrations_dir)` / `DB.scaffold_all(...)`

### 3. Generated Model Output Example

Auto-generated at `scripts/models/user.lua`:

```lua
-- ═══════════════════════════════════════════════════════════════════════════
-- Model: User
-- Table: users
-- Auto-generated by Eloquent Scaffold Generator
-- ═══════════════════════════════════════════════════════════════════════════

local Model = _G.Model or require("eloquent").Model

---@class User
---@field id number Primary Key
---@field name string
---@field email string
---@field created_at string
---@field updated_at string

local User = Model.extend("users", {
    primary_key = "id",
    timestamps = true
})

return User
```

### 4. Generated Migration Output Example

Auto-generated at `scripts/migrations/2026_10_02_172842_create_users_table.lua`:

```lua
-- ═══════════════════════════════════════════════════════════════════════════
-- Migration: create_users_table
-- Table: users
-- Auto-generated by Eloquent Scaffold Generator
-- ═══════════════════════════════════════════════════════════════════════════

local Schema = _G.Schema or require("eloquent").Schema

return {
    up = function()
        return Schema.create("users", function(table)
            table:id()
            table:string("name")
            table:string("email")
            table:timestamps()
        end)
    end,

    down = function()
        return Schema.dropIfExists("users")
    end
}
```


