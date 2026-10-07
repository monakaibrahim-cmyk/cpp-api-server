# API Framework

High-performance, asynchronous C++ service framework featuring dynamic Lua scripting, an Eloquent-style ORM query builder with database scaffolding, an Apache `.htaccess` emulation layer, and a real-time terminal dashboard.

## Overview

The **API Framework** provides an enterprise-ready foundation for developing RESTful APIs and distributed microservices. It bridges low-latency C++ network I/O with high-level Lua scripting, allowing endpoints, middlewares, database models, and URL rewrite rules to be defined and updated dynamically without recompiling the core application.

### Key Architecture Components

- **Core Server & Routing**:
  - Asynchronous event-driven HTTP listener powered by [Crow](https://crowcpp.org/).
  - Thread-safe routing table supporting both native C++ handler callbacks and dynamic Lua route dispatching.
  - Multi-threaded worker thread pool for background task offloading and asynchronous execution.
  - Thread-safe dependency injection via `service_registry`.

- **Dynamic Lua Engine**:
  - Embedded Lua runtime (5.5 default; 5.4, 5.3, 5.2 selectable via `-DLUA_VERSION`) with [Sol2](https://github.com/ThePhD/sol2) bindings.
  - Full exposure of developer-defined C++ classes, structs, namespaces, variables, and functions.
  - Dynamic route registration (`route("GET", "/path", function(req, res) ... end)`).
  - Dynamic file routing with automated prefix-based path resolution.

- **Local Ollama Model Integration & Auto-Discovery**:
  - Direct communication with local Ollama daemon (`http://127.0.0.1:11434`) via asynchronous Boost.Beast HTTP client.
  - Auto-discovery of installed local models / agents (`GET /api/agents` & `/api/tags`).
  - Client session management via in-memory LRU cache identified by user IP.
  - Multi-turn conversation and chat history caching (`chat_history:<user_ip>`).
  - Endpoints for status (`/api/ollama/status`), agents (`/api/agents`), chat (`/api/chat`), history (`/api/chat/history`), and session clearance.

- **Apache `.htaccess` Emulation**:
  - Native parser and evaluator for standard Apache configuration directives.
  - Support for `RewriteEngine`, `RewriteCond`, `RewriteRule` (flags: `[L]`, `[R=301]`, `[F]`, `[G]`, `[NC]`, `[QSA]`).
  - Security directives including `Order Allow,Deny`, `Deny from`, `Allow from`, and `Require ip`.
  - HTTP response header manipulation (`Header set`, `Header append`, `Header unset`).
  - Custom error handlers (`ErrorDocument 404 /err404.html`).
  - Configurable priority: user-provided `.htaccess` takes precedence, falling back gracefully to Lua route configurations.

- **Observability & Dashboard**:
  - Interactive Terminal User Interface (TUI) powered by [FTXUI](https://github.com/ArthurSonzogni/FTXUI).
  - Real-time CPU, virtual memory, resident memory, and system load sampling via Linux `/proc`.
  - Sliding-window throughput tracking (requests per second, bytes in/out, HTTP status codes, latency percentiles).
  - Ring buffer logging system with multi-channel severity filtering and live TUI streaming.
  - LRU memory cache with thread-safe reader/writer locking and hit/miss eviction statistics.

- **Modular Extension System**:
  - Dynamic module registration using `API_REGISTER_MODULE` and `API_REGISTER_SCRIPT`.
  - Comprehensive lifecycle hooks: `on_config_load`, `on_init`, `on_handlers_register`, `on_lua_init`, `on_request_override`, `on_before_request`, `on_after_request`, `on_server_startup`, and `on_server_shutdown`.
  - Automated module generation utility via `./modules/create_module.sh`.

---

## Directory Structure

```text
.
├── CMakeLists.txt              # Primary CMake build configuration (C++26)
├── Doxyfile                    # Doxygen configuration using Doxygen Awesome CSS
├── config/
│   ├── server.lua              # Server runtime configuration
│   └── .htaccess.dist          # Sample Apache .htaccess configuration
├── docs/
│   ├── header.html             # Custom HTML header enabling Awesome extensions
│   ├── custom.css              # Custom brand styling and typography overrides
│   └── doxygen-awesome-css/    # Modern Doxygen theme assets & JavaScript extensions
├── include/
│   └── api/                    # Core C++ public API headers (.h)
│       ├── cache.h             # Thread-safe LRU cache with shared_mutex
│       ├── config.h            # ServerConfig and Lua configuration loader
│       ├── connection_tracker.h# Sliding-window connection and traffic metrics
│       ├── dashboard.h         # FTXUI interactive terminal dashboard
│       ├── db_driver.h         # Abstract database driver interface & value variants
│       ├── htaccess.h          # Apache .htaccess directive parser & evaluator
│       ├── logger.h            # Boost.Log severity channel logger and ring buffer
│       ├── lua_binding.h       # Sol2 namespace and usertype binding utilities
│       ├── lua_engine.h        # Embedded Lua state and script engine
│       ├── metrics.h           # System resource and CPU/memory sampler
│       ├── middleware.h        # Crow HTTP middlewares (CORS, htaccess, metrics)
│       ├── module.h            # Modular extension component base alias
│       ├── module_registry.h   # Module discovery registry
│       ├── router.h            # Unified C++ and Lua HTTP request router
│       ├── script_mgr.h        # Central lifecycle hook manager and dispatchers
│       ├── server.h            # Asynchronous Crow HTTP server wrapper
│       ├── service_registry.h  # Thread-safe dependency injection service locator
│       └── thread_pool.h       # Asynchronous FIFO worker thread pool
├── modules/
│   ├── create_module.sh        # Shell script to scaffold new C++ modules
│   ├── mod_jwt/                # RFC 7519 JWT signing and verification module
│   ├── mod_ollama/             # Ollama AI integration, agent discovery & session cache
│   └── mod_template/           # Template module illustrating class & struct Lua bindings
├── scripts/
│   ├── routes.lua              # Application HTTP routes defined in Lua
│   └── test_ollama_integration.py # Integration test suite for Ollama endpoints
└── src/                        # Core C++ implementation sources (.cpp)
    ├── core/                   # Core infrastructure (cache, config, logger, metrics)
    ├── dashboard/              # FTXUI dashboard rendering logic
    ├── scripting/              # Lua engine and script manager implementations
    └── server/                 # Crow server, connection tracker, and routing logic
```

---

## Building the Project

### Prerequisites

- Modern C++ compiler with C++26/C++23 support (GCC 14+ or Clang 19+)
- CMake 3.25+
- Boost libraries (`libboost-log-dev`, `libboost-thread-dev`, `libboost-system-dev`, `libboost-dev`)
- OpenSSL development headers (`libssl-dev`)
- Doxygen 1.9.5+ and Graphviz (`dot`) for documentation generation (optional)

### Automated Dependency Installation (Debian & Ubuntu)

To automatically install all dependencies on Debian (12/13+) and Ubuntu (22.04/24.04+):

```bash
# Run the installation script (uses sudo when running as non-root)
./install_dependencies.sh

# Non-interactive mode (e.g. for CI/Docker)
./install_dependencies.sh -y

# Optional flags
./install_dependencies.sh --no-docs      # Skip Doxygen & Graphviz
./install_dependencies.sh --with-ollama  # Also install local Ollama CLI daemon
./install_dependencies.sh --dry-run      # Preview operations without installing
```

### Compilation

```bash
# Generate build configuration
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile all targets
cmake --build build -j$(nproc)
```

### Selecting the Lua Version

The embedded Lua runtime is chosen at configure time with `LUA_VERSION` (default `5.5`):

| `-DLUA_VERSION=`     | Release built |
| -------------------- | ------------- |
| `5.5` (experimental) | Lua 5.5.1     |
| `5.4` (default)      | Lua 5.4.8     |
| `5.3`                | Lua 5.3.6     |
| `5.2`                | Lua 5.2.4     |

```bash
cmake -B build -DLUA_VERSION=5.4
```

Sources are downloaded from the official `lua.org` release tarballs and verified against pinned SHA-256 hashes. Re-running `cmake` with a different value in an existing build directory triggers a full rebuild of Lua and every Sol2 consumer.

> Lua scripts must use syntax supported by the selected version (e.g. integer division `//` and bitwise operators require 5.3+, `<const>`/`<close>` require 5.4+).

---

## Running the Server

```bash
# Launch server with interactive FTXUI dashboard (default)
./build/API-cli

# Run in background daemon / headless mode
./build/API-cli --headless

# Run in foreground without terminal UI (standard console logs)
./build/API-cli --no-dashboard

# Override listening port and configuration path
./build/API-cli --config config/server.lua --port 8080
```

---

## License

This project is licensed under the terms described in the [LICENSE](LICENSE) file.
