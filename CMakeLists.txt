cmake_minimum_required(VERSION 3.25)
project(API-cli LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ── FTXUI ──────────────────────────────────────────────────────────
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
    GIT_TAG v5.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ftxui)

# ── SQLite3 ────────────────────────────────────────────────────────
FetchContent_Declare(sqlite3
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
)
FetchContent_MakeAvailable(sqlite3)
add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})

# ── Crow (header-only HTTP framework) ─────────────────────────────
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CROW_USE_BOOST ON CACHE BOOL "" FORCE)
FetchContent_Declare(Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow
    GIT_TAG v1.2.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Crow)

# Ensure Crow populates remote_ip_address on unmatched/404 routes
set(CROW_CONN_HEADER "${crow_SOURCE_DIR}/include/crow/http_connection.h")
if(EXISTS "${CROW_CONN_HEADER}")
    file(READ "${CROW_CONN_HEADER}" CROW_CONN_CONTENT)
    string(FIND "${CROW_CONN_CONTENT}" "req_.remote_ip_address = adaptor_.remote_endpoint().address().to_string();\n            routing_handle_result_" CROW_ALREADY_PATCHED)
    if(CROW_ALREADY_PATCHED EQUAL -1)
        string(REPLACE
            "routing_handle_result_ = handler_->handle_initial(req_, res);"
            "try { req_.remote_ip_address = adaptor_.remote_endpoint().address().to_string(); } catch (...) {}\n            routing_handle_result_ = handler_->handle_initial(req_, res);"
            CROW_PATCHED_CONTENT "${CROW_CONN_CONTENT}")
        file(WRITE "${CROW_CONN_HEADER}" "${CROW_PATCHED_CONTENT}")
    endif()
endif()

# ── Lua 5.4 (built from source) ───────────────────────────────────
FetchContent_Declare(lua
    URL https://codeload.github.com/lua/lua/tar.gz/refs/tags/v5.4.7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
endif()

file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c ${lua_SOURCE_DIR}/src/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
add_library(lua_lib STATIC ${LUA_SOURCES})
target_include_directories(lua_lib PUBLIC ${lua_SOURCE_DIR} ${lua_SOURCE_DIR}/src)
target_compile_definitions(lua_lib PUBLIC LUA_USE_POSIX)

# ── sol2 (header-only Lua binding) ────────────────────────────────
FetchContent_Declare(sol2
    URL https://codeload.github.com/ThePhD/sol2/tar.gz/refs/tags/v3.3.1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(sol2)
if(NOT sol2_POPULATED)
    FetchContent_Populate(sol2)
endif()

# ── Sources ────────────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC
    include
    src
    ${sol2_SOURCE_DIR}/include
)

# ── Boost ──────────────────────────────────────────────────────────
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS
    log
    log_setup
    thread
)

# ── Link ───────────────────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    Crow::Crow
    Boost::headers
    Boost::log
    Boost::log_setup
    Boost::thread
    Threads::Threads
    sqlite3
    lua_lib
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    SOL_ALL_SAFETIES_ON=1
)

# ── Modules Auto-Discovery (AzerothCore style) ───────────────────
file(GLOB MODULE_DIRS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/modules/*")
set(MODULE_LIBRARIES "")

foreach(MODULE_DIR ${MODULE_DIRS})
    if(IS_DIRECTORY ${MODULE_DIR})
        get_filename_component(MOD_NAME ${MODULE_DIR} NAME)
        if(NOT MOD_NAME MATCHES "^\\." AND NOT MOD_NAME STREQUAL "mod_template" AND NOT MOD_NAME MATCHES "\\.disabled$")
            if(EXISTS "${MODULE_DIR}/CMakeLists.txt")
                message(STATUS "[Module] Discovered: ${MOD_NAME}")
                add_subdirectory(${MODULE_DIR})
                list(APPEND MODULE_LIBRARIES ${MOD_NAME})
            endif()
        endif()
    endif()
endforeach()

if(MODULE_LIBRARIES)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        -Wl,--whole-archive
        ${MODULE_LIBRARIES}
        -Wl,--no-whole-archive
    )
endif()

# ── Copy config/scripts to build dir for development ──────────────
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/scripts
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts
)
