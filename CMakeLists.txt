cmake_minimum_required(VERSION 3.25)
project(API-cli LANGUAGES C CXX)

# Project options

set(CMAKE_CXX_STANDARD 26 CACHE STRING "C++ Standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 23 CACHE STRING "C Standard")
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(API_CLI_BUILD_DOCS
    "Enable Doxygen documentation target"
    OFF
)

option(API_CLI_USE_STATIC_BOOST
    "Use static Boost libraries"
    OFF
)

option(API_CLI_ENABLE_LUA_POSIX
    "Enable LUA_USE_POSIX on POSIX platforms"
    ON
)

# Platform detection

if(WIN32)
    set(API_CLI_PLATFORM_WINDOWS TRUE)
elseif(UNIX)
    set(API_CLI_PLATFORM_UNIX TRUE)
endif()

if(MSVC)
    set(API_CLI_COMPILER_MSVC TRUE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(API_CLI_COMPILER_GNU TRUE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(API_CLI_COMPILER_CLANG TRUE)
endif()

message(STATUS "")
message(STATUS "==============================================")
message(STATUS " API-cli configuration")
message(STATUS "==============================================")
message(STATUS "Platform:       ${CMAKE_SYSTEM_NAME}")
message(STATUS "C compiler:     ${CMAKE_C_COMPILER_ID}")
message(STATUS "C++ compiler:   ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ standard:   C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "==============================================")
message(STATUS "")

# Global compile configuration

if(MSVC)
    add_compile_options(
        /W4
        /permissive-
        /EHsc
    )

    # Prevent Windows headers from defining min/max macros.
    add_compile_definitions(
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# Dependencies

include(FetchContent)

# FTXUI

message(STATUS "[Dependency] Fetching FTXUI")

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui.git
    GIT_TAG v5.0.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(ftxui)

# Crow

message(STATUS "[Dependency] Fetching Crow")

set(CROW_BUILD_EXAMPLES OFF CACHE BOOL
    "Disable Crow examples"
    FORCE
)

set(CROW_BUILD_TESTS OFF CACHE BOOL
    "Disable Crow tests"
    FORCE
)

set(CROW_USE_BOOST ON CACHE BOOL
    "Use Boost with Crow"
    FORCE
)

FetchContent_Declare(
    Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(Crow)

# Lua 5.4

message(STATUS "[Dependency] Fetching Lua 5.4.7")

FetchContent_Declare(
    lua
    URL https://codeload.github.com/lua/lua/tar.gz/refs/tags/v5.4.7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_GetProperties(lua)

if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
endif()

# Lua source files

file(
    GLOB LUA_SOURCES
    CONFIGURE_DEPENDS
    "${lua_SOURCE_DIR}/*.c"
    "${lua_SOURCE_DIR}/src/*.c"
)

# Do not compile Lua's standalone executables into the library.
list(
    FILTER LUA_SOURCES
    EXCLUDE REGEX
    ".*/(lua|luac)\\.c$"
)

if(NOT LUA_SOURCES)
    message(FATAL_ERROR
        "Could not find Lua source files in ${lua_SOURCE_DIR}"
    )
endif()

add_library(lua_lib STATIC
    ${LUA_SOURCES}
)

add_library(lua::lua ALIAS lua_lib)

target_include_directories(lua_lib
    PUBLIC
        "${lua_SOURCE_DIR}"
        "${lua_SOURCE_DIR}/src"
)

# Lua needs the POSIX configuration on Unix-like systems.
# Do NOT define LUA_USE_POSIX for native Windows builds.
if(UNIX AND API_CLI_ENABLE_LUA_POSIX)
    target_compile_definitions(lua_lib
        PUBLIC
            LUA_USE_POSIX
    )
endif()

# Windows-specific Lua configuration.
if(WIN32)
    target_compile_definitions(lua_lib
        PRIVATE
            LUA_BUILD_AS_DLL=0
    )
endif()

# Lua uses C, so make sure it is compiled with the requested C standard.
set_target_properties(lua_lib PROPERTIES
    C_STANDARD ${CMAKE_C_STANDARD}
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# sol2

message(STATUS "[Dependency] Fetching sol2")

FetchContent_Declare(
    sol2
    URL https://codeload.github.com/ThePhD/sol2/tar.gz/refs/tags/v3.3.1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_GetProperties(sol2)

if(NOT sol2_POPULATED)
    FetchContent_Populate(sol2)
endif()

# Boost

message(STATUS "[Dependency] Finding Boost")

cmake_policy(SET CMP0144 NEW)

if(API_CLI_USE_STATIC_BOOST)
    set(Boost_USE_STATIC_LIBS ON)
else()
    set(Boost_USE_STATIC_LIBS OFF)
endif()

set(Boost_USE_MULTITHREADED ON)
set(Boost_NO_BOOST_CMAKE OFF)

find_package(
    Boost
    REQUIRED
    COMPONENTS
        log
        log_setup
        thread
        system
)

find_package(Threads REQUIRED)

# Main executable

file(
    GLOB_RECURSE SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

if(NOT SOURCES)
    message(FATAL_ERROR
        "No source files found under ${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
endif()

add_executable(
    ${PROJECT_NAME}
    ${SOURCES}
)

# Include directories

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${sol2_SOURCE_DIR}/include"
)

# C++ configuration

set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES
        CXX_STANDARD ${CMAKE_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

# Windows configuration

if(WIN32)

    target_compile_definitions(
        ${PROJECT_NAME}
        PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )

    # Disable console warning about deprecated CRT functions only if
    # required by dependencies.
    target_compile_definitions(
        ${PROJECT_NAME}
        PRIVATE
            _CRT_SECURE_NO_WARNINGS
    )

endif()

# Link dependencies

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE

        # FTXUI
        ftxui::screen
        ftxui::dom
        ftxui::component

        # Crow
        Crow::Crow

        # Boost
        Boost::headers
        Boost::log
        Boost::log_setup
        Boost::thread
        Boost::system

        # Threading
        Threads::Threads

        # Lua
        lua::lua
)

# sol2 configuration

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
        SOL_ALL_SAFETIES_ON=1
)

# Modules

message(STATUS "")
message(STATUS "==============================================")
message(STATUS " Module discovery")
message(STATUS "==============================================")

file(
    GLOB MODULE_DIRS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/*"
)

set(MODULE_LIBRARIES "")

foreach(MODULE_DIR IN LISTS MODULE_DIRS)

    if(NOT IS_DIRECTORY "${MODULE_DIR}")
        continue()
    endif()

    get_filename_component(
        MOD_NAME
        "${MODULE_DIR}"
        NAME
    )

    # Ignore:
    #   .hidden directories
    #   mod-template / mod_template
    #   *.disabled directories
    if(
        MOD_NAME MATCHES "^\\."
        OR MOD_NAME STREQUAL "mod_template"
        OR MOD_NAME STREQUAL "mod-template"
        OR MOD_NAME MATCHES "\\.disabled$"
    )
        continue()
    endif()

    if(EXISTS "${MODULE_DIR}/CMakeLists.txt")

        message(STATUS "[Module] Discovered: ${MOD_NAME}")

        add_subdirectory(
            "${MODULE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/modules/${MOD_NAME}"
        )

        # Assumes the module CMakeLists creates a target with the same
        # name as the module directory.
        if(TARGET "${MOD_NAME}")

            list(
                APPEND
                MODULE_LIBRARIES
                "${MOD_NAME}"
            )

        else()

            message(WARNING
                "[Module] ${MOD_NAME} has a CMakeLists.txt but did not "
                "create a target named '${MOD_NAME}'."
            )

        endif()

    endif()

endforeach()

message(STATUS "==============================================")
message(STATUS "")

# Module linking

if(MODULE_LIBRARIES)

    message(STATUS
        "[Module] Linking: ${MODULE_LIBRARIES}"
    )

    # MSVC
    #
    # MSVC does not understand:
    #
    #     -Wl,--whole-archive
    #
    # Instead use /WHOLEARCHIVE.

    if(MSVC)

        foreach(MODULE IN LISTS MODULE_LIBRARIES)

            target_link_libraries(
                ${PROJECT_NAME}
                PRIVATE
                    "${MODULE}"
            )

            target_link_options(
                ${PROJECT_NAME}
                PRIVATE
                    "/WHOLEARCHIVE:$<TARGET_FILE:${MODULE}>"
            )

        endforeach()

    # GCC / Clang / MinGW

    elseif(
        CMAKE_CXX_COMPILER_ID MATCHES
        "GNU|Clang"
    )

        target_link_libraries(
            ${PROJECT_NAME}
            PRIVATE
                "-Wl,--whole-archive"
                ${MODULE_LIBRARIES}
                "-Wl,--no-whole-archive"
        )

    # Unknown toolchain

    else()

        message(WARNING
            "Unknown compiler '${CMAKE_CXX_COMPILER_ID}'. "
            "Linking modules normally without whole-archive."
        )

        target_link_libraries(
            ${PROJECT_NAME}
            PRIVATE
                ${MODULE_LIBRARIES}
        )

    endif()

endif()

# Runtime asset copying

set(API_CLI_CONFIG_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
)

set(API_CLI_SCRIPTS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
)

# Validate optional runtime directories

if(NOT EXISTS "${API_CLI_CONFIG_DIR}")
    message(WARNING
        "Config directory does not exist: ${API_CLI_CONFIG_DIR}"
    )
endif()

if(NOT EXISTS "${API_CLI_SCRIPTS_DIR}")
    message(WARNING
        "Scripts directory does not exist: ${API_CLI_SCRIPTS_DIR}"
    )
endif()

# Copy config

if(EXISTS "${API_CLI_CONFIG_DIR}")

    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD

        COMMAND
            ${CMAKE_COMMAND}
            -E
            make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config"

        COMMAND
            ${CMAKE_COMMAND}
            -E
            copy_directory
            "${API_CLI_CONFIG_DIR}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config"

        VERBATIM
    )

endif()

# Copy scripts

if(EXISTS "${API_CLI_SCRIPTS_DIR}")

    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD

        COMMAND
            ${CMAKE_COMMAND}
            -E
            make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts"

        COMMAND
            ${CMAKE_COMMAND}
            -E
            copy_directory
            "${API_CLI_SCRIPTS_DIR}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts"

        VERBATIM
    )

endif()

# Doxygen

if(API_CLI_BUILD_DOCS)

    find_package(
        Doxygen
        OPTIONAL_COMPONENTS dot
    )

    if(DOXYGEN_FOUND)

        add_custom_target(
            docs

            COMMAND
                ${DOXYGEN_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile"

            WORKING_DIRECTORY
                "${CMAKE_CURRENT_SOURCE_DIR}"

            COMMENT
                "Generating API documentation with Doxygen Awesome"

            VERBATIM
        )

        message(STATUS "[Docs] Doxygen found: ${DOXYGEN_EXECUTABLE}")

        if(DOXYGEN_DOT_FOUND)
            message(STATUS "[Docs] Graphviz dot found")
        else()
            message(STATUS
                "[Docs] Graphviz dot not found; diagrams disabled"
            )
        endif()

    else()

        message(STATUS
            "[Docs] Doxygen not found; 'docs' target disabled"
        )

    endif()

endif()

# Installation

include(GNUInstallDirs)

install(
    TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(EXISTS "${API_CLI_CONFIG_DIR}")

    install(
        DIRECTORY
            "${API_CLI_CONFIG_DIR}/"
        DESTINATION
            "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/config"
    )

endif()

if(EXISTS "${API_CLI_SCRIPTS_DIR}")

    install(
        DIRECTORY
            "${API_CLI_SCRIPTS_DIR}/"
        DESTINATION
            "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/scripts"
    )

endif()

# Final configuration summary

message(STATUS "")
message(STATUS "==============================================")
message(STATUS " Configuration complete")
message(STATUS "==============================================")
message(STATUS "Project:        ${PROJECT_NAME}")
message(STATUS "Version:        ${PROJECT_VERSION}")
message(STATUS "Platform:       ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler:       ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ standard:   C++${CMAKE_CXX_STANDARD}")
message(STATUS "Boost static:   ${API_CLI_USE_STATIC_BOOST}")
message(STATUS "Modules:        ${MODULE_LIBRARIES}")
message(STATUS "Docs:           ${API_CLI_BUILD_DOCS}")
message(STATUS "==============================================")
message(STATUS "")
