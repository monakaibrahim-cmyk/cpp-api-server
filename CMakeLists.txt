cmake_minimum_required(VERSION 3.25)
project(API-cli LANGUAGES C CXX)

function(message_box)
    set(top_left     "╭")
    set(top_right    "╮")
    set(bottom_left  "╰")
    set(bottom_right "╯")
    set(horiz        "─")
    set(vert         "│")

    set(padding "  ")

    # ============================================================
    # Collect input
    # ============================================================

    set(lines "")

    foreach(line IN LISTS ARGN)
        list(APPEND lines "${line}")
    endforeach()

    # ============================================================
    # Find maximum widths
    # ============================================================

    set(max_key_width   0)
    set(max_value_width 0)
    set(max_text_width  0)

    foreach(line IN LISTS lines)

        if(line STREQUAL "--DIVIDER--")
            continue()
        endif()

        if(line MATCHES "^([^:]+):(.*)$")

            set(key "${CMAKE_MATCH_1}")
            set(value "${CMAKE_MATCH_2}")

            string(STRIP "${key}" key)
            string(STRIP "${value}" value)

            string(LENGTH "${key}" key_len)
            string(LENGTH "${value}" value_len)

            if(key_len GREATER max_key_width)
                set(max_key_width "${key_len}")
            endif()

            if(value_len GREATER max_value_width)
                set(max_value_width "${value_len}")
            endif()

        else()

            string(LENGTH "${line}" text_len)

            if(text_len GREATER max_text_width)
                set(max_text_width "${text_len}")
            endif()

        endif()

    endforeach()

    # ============================================================
    # Calculate widths
    # ============================================================

    math(
        EXPR
        field_width
        "${max_key_width} + 2 + ${max_value_width}"
    )

    set(content_width "${max_text_width}")

    if(field_width GREATER content_width)
        set(content_width "${field_width}")
    endif()

    if(content_width LESS 4)
        set(content_width 4)
    endif()

    math(
        EXPR
        border_width
        "${content_width} + 4"
    )

    string(REPEAT "${horiz}" "${border_width}" border)
    string(REPEAT "${horiz}" "${content_width}" divider)

    # ============================================================
    # Top
    # ============================================================

    message(STATUS
        "${top_left}${border}${top_right}"
    )

    # ============================================================
    # Render
    # ============================================================

    set(previous_was_key_value FALSE)

    foreach(line IN LISTS lines)

        # ========================================================
        # Divider
        # ========================================================

        if(line STREQUAL "--DIVIDER--")

            message(STATUS
                "${vert}${padding}"
                "${divider}"
                "${padding}${vert}"
            )

            set(previous_was_key_value FALSE)

            continue()

        endif()

        # ========================================================
        # Key/value
        # ========================================================

        if(line MATCHES "^([^:]+):(.*)$")

            set(key "${CMAKE_MATCH_1}")
            set(value "${CMAKE_MATCH_2}")

            string(STRIP "${key}" key)
            string(STRIP "${value}" value)

            string(LENGTH "${key}" key_len)
            string(LENGTH "${value}" value_len)

            # ----------------------------------------------------
            # Align key to the colon.
            # ----------------------------------------------------

            math(
                EXPR
                key_padding
                "${max_key_width} - ${key_len}"
            )

            set(key_spaces "")

            if(key_padding GREATER 0)
                string(
                    REPEAT
                    " "
                    "${key_padding}"
                    key_spaces
                )
            endif()

            # ----------------------------------------------------
            # Align value to the right.
            # ----------------------------------------------------

            math(
                EXPR
                value_padding
                "${max_value_width} - ${value_len}"
            )

            set(value_spaces "")

            if(value_padding GREATER 0)
                string(
                    REPEAT
                    " "
                    "${value_padding}"
                    value_spaces
                )
            endif()

            message(STATUS
                "${vert}${padding}"
                "${key}${key_spaces}: "
                "${value_spaces}${value}"
                "${padding}${vert}"
            )

            # ----------------------------------------------------
            # Remember that the next CMake-list item is a
            # continuation of this field.
            # ----------------------------------------------------

            set(previous_was_key_value TRUE)

            # Number of spaces before continuation value:
            #
            # padding + max_key_width + ": "
            #
            math(
                EXPR
                continuation_indent
                "${max_key_width} + 2"
            )

            set(continuation_spaces "")

            if(continuation_indent GREATER 0)
                string(
                    REPEAT
                    " "
                    "${continuation_indent}"
                    continuation_spaces
                )
            endif()

            continue()

        endif()

        # ========================================================
        # Continuation of previous key/value
        #
        # Example:
        #
        # Modules:      mod_jwt
        #               mod_orm
        #
        # The continuation value is right-aligned.
        # ========================================================

        if(previous_was_key_value)

            string(LENGTH "${line}" value_len)

            math(
                EXPR
                value_padding
                "${max_value_width} - ${value_len}"
            )

            set(value_spaces "")

            if(value_padding GREATER 0)
                string(
                    REPEAT
                    " "
                    "${value_padding}"
                    value_spaces
                )
            endif()

            message(STATUS
                "${vert}${padding}"
                "${continuation_spaces}"
                "${value_spaces}${line}"
                "${padding}${vert}"
            )

            continue()

        endif()

        # ========================================================
        # Normal text
        # ========================================================

        string(LENGTH "${line}" text_len)

        math(
            EXPR
            remaining
            "${content_width} - ${text_len}"
        )

        math(
            EXPR
            left_padding
            "${remaining} / 2"
        )

        math(
            EXPR
            right_padding
            "${remaining} - ${left_padding}"
        )

        set(left_spaces "")
        set(right_spaces "")

        if(left_padding GREATER 0)
            string(REPEAT " " "${left_padding}" left_spaces)
        endif()

        if(right_padding GREATER 0)
            string(REPEAT " " "${right_padding}" right_spaces)
        endif()

        message(STATUS
            "${vert}${padding}"
            "${left_spaces}${line}${right_spaces}"
            "${padding}${vert}"
        )

        set(previous_was_key_value FALSE)

    endforeach()

    # ============================================================
    # Bottom
    # ============================================================

    message(STATUS
        "${bottom_left}${border}${bottom_right}"
    )

endfunction()

# Project options

set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 23 CACHE STRING "C standard")
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

# Embedded Lua runtime series. Usage: cmake -B build -DLUA_VERSION=5.4
set(API_CLI_SUPPORTED_LUA_VERSIONS 5.5 5.4 5.3 5.2)

set(LUA_VERSION "5.4" CACHE STRING
    "Embedded Lua version series (5.5, 5.4, 5.3 or 5.2)"
)

set_property(CACHE LUA_VERSION PROPERTY STRINGS ${API_CLI_SUPPORTED_LUA_VERSIONS})

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
message_box(
    "API-cli configuration"
    "--DIVIDER--"
    "Platform: ${CMAKE_SYSTEM_NAME}"
    "C Compiler: ${CMAKE_C_COMPILER_ID}"
    "C++ Compiler: C++${CMAKE_CXX_COMPILER_ID}"
    "C++ standard: ${CMAKE_CXX_STANDARD}"
    "Build type: ${CMAKE_BUILD_TYPE}"
)
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
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
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
    GIT_TAG v7.0.3
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

# Lua (selected through -DLUA_VERSION)

if(NOT LUA_VERSION IN_LIST API_CLI_SUPPORTED_LUA_VERSIONS)
    message(FATAL_ERROR
        "Unsupported LUA_VERSION '${LUA_VERSION}'. "
        "Choose one of: ${API_CLI_SUPPORTED_LUA_VERSIONS}"
    )
endif()

# Latest official release of each series, pinned with its SHA-256 hash.
if(LUA_VERSION STREQUAL "5.5")
    set(API_CLI_LUA_RELEASE "5.5.1")
    set(API_CLI_LUA_HASH "1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce")
elseif(LUA_VERSION STREQUAL "5.4")
    set(API_CLI_LUA_RELEASE "5.4.8")
    set(API_CLI_LUA_HASH "4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae")
elseif(LUA_VERSION STREQUAL "5.3")
    set(API_CLI_LUA_RELEASE "5.3.6")
    set(API_CLI_LUA_HASH "fc5fd69bb8736323f026672b1b7235da613d7177e72558893a0bdcd320466d60")
elseif(LUA_VERSION STREQUAL "5.2")
    set(API_CLI_LUA_RELEASE "5.2.4")
    set(API_CLI_LUA_HASH "b9e2e4aad6789b3b63a056d442f7b39f0ecfca3ae0f1fc0ae4e9614401b69f4b")
endif()

message(STATUS "[Dependency] Fetching Lua ${API_CLI_LUA_RELEASE} (LUA_VERSION=${LUA_VERSION})")

cmake_policy(SET CMP0169 OLD)

# A version-specific content name gives every release its own source
# directory, so switching LUA_VERSION recompiles Lua and every sol2
# consumer instead of linking a stale archive built from other headers.
string(REPLACE "." "_" API_CLI_LUA_CONTENT_NAME "lua_${API_CLI_LUA_RELEASE}")

FetchContent_Declare(
    ${API_CLI_LUA_CONTENT_NAME}
    URL https://www.lua.org/ftp/lua-${API_CLI_LUA_RELEASE}.tar.gz
    URL_HASH SHA256=${API_CLI_LUA_HASH}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_GetProperties(${API_CLI_LUA_CONTENT_NAME})

if(NOT ${API_CLI_LUA_CONTENT_NAME}_POPULATED)
    FetchContent_Populate(${API_CLI_LUA_CONTENT_NAME})
endif()

set(lua_SOURCE_DIR "${${API_CLI_LUA_CONTENT_NAME}_SOURCE_DIR}")

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
    ".*/(lua|luac|onelua)\\.c$"
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

    # Lua 5.3+ exposes POSIX/XSI declarations (_longjmp, popen, mkstemp)
    # through lprefix.h. Lua 5.2 lacks that header, so strict C mode
    # (C_EXTENSIONS OFF) hides them unless the feature macro is set here.
    if(LUA_VERSION STREQUAL "5.2")
        target_compile_definitions(lua_lib
            PRIVATE
                _XOPEN_SOURCE=600
        )
    endif()
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
    URL https://codeload.github.com/ThePhD/sol2/tar.gz/refs/tags/v3.5.0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_GetProperties(sol2)

if(NOT sol2_POPULATED)
    FetchContent_Populate(sol2)
endif()

# Apply sol2 compatibility adjustments for Lua 5.5 (upstream PR #1723)
set(SOL2_COMPAT_53_FILE "${sol2_SOURCE_DIR}/include/sol/compatibility/compat-5.3.h")
if(EXISTS "${SOL2_COMPAT_53_FILE}")
    file(READ "${SOL2_COMPAT_53_FILE}" SOL2_COMPAT_53_CONTENT)
    string(REPLACE "LUA_VERSION_NUM > 504" "LUA_VERSION_NUM > 505" SOL2_COMPAT_53_CONTENT "${SOL2_COMPAT_53_CONTENT}")
    file(WRITE "${SOL2_COMPAT_53_FILE}" "${SOL2_COMPAT_53_CONTENT}")
endif()

set(SOL2_COMPAT_54_FILE "${sol2_SOURCE_DIR}/include/sol/compatibility/compat-5.4.h")
if(EXISTS "${SOL2_COMPAT_54_FILE}")
    file(READ "${SOL2_COMPAT_54_FILE}" SOL2_COMPAT_54_CONTENT)
    string(REPLACE "LUA_VERSION_NUM == 504" "LUA_VERSION_NUM >= 504" SOL2_COMPAT_54_CONTENT "${SOL2_COMPAT_54_CONTENT}")
    file(WRITE "${SOL2_COMPAT_54_FILE}" "${SOL2_COMPAT_54_CONTENT}")
endif()

set(SOL2_STATE_FILE "${sol2_SOURCE_DIR}/include/sol/state.hpp")
if(EXISTS "${SOL2_STATE_FILE}")
    file(READ "${SOL2_STATE_FILE}" SOL2_STATE_CONTENT)
    if(NOT SOL2_STATE_CONTENT MATCHES "sol_lua_newstate")
        string(REPLACE "namespace sol {" "inline lua_State* sol_lua_newstate(lua_Alloc f, void* ud, [[maybe_unused]] unsigned seed = 0) {\n#if LUA_VERSION_NUM >= 505\n\treturn ::lua_newstate(f, ud, seed);\n#else\n\treturn ::lua_newstate(f, ud);\n#endif\n}\n\nnamespace sol {" SOL2_STATE_CONTENT "${SOL2_STATE_CONTENT}")
        string(REPLACE ": unique_base(lua_newstate(alfunc, alpointer))" ": unique_base(sol_lua_newstate(alfunc, alpointer))" SOL2_STATE_CONTENT "${SOL2_STATE_CONTENT}")
        file(WRITE "${SOL2_STATE_FILE}" "${SOL2_STATE_CONTENT}")
    endif()
endif()

# Boost

message(STATUS "[Dependency] Finding Boost")

cmake_policy(SET CMP0167 NEW) 
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
)

message(STATUS "Boost_VERSION: ${Boost_VERSION}")
message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost_LIBRARY_DIRS: ${Boost_LIBRARY_DIRS}")
message(STATUS "Boost_DIR: ${Boost_DIR}")

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

        # Threading
        Threads::Threads

        # Lua
        lua::lua
)

# sol2 and boost configuration

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
        SOL_ALL_SAFETIES_ON=1
        BOOST_ALL_NO_LIB
)

# Modules
set(BOX_CONTENTS 
    "Module discovery"
    "--DIVIDER--"
)

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
    #   mod_template
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

        list(APPEND BOX_CONTENTS "[Module] ${MOD_NAME}")

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

list(LENGTH BOX_CONTENTS current_len)

if(current_len EQUAL 2)
    list(APPEND BOX_CONTENTS "No modules discovered.")
endif()

message(STATUS "")
message_box(${BOX_CONTENTS})
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

set(API_CLI_ASSETS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/public"
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

if(NOT EXISTS "${API_CLI_ASSETS_DIR}")
    message(WARNING
        "Assets directory does not exists: ${API_CLI_ASSETS_DIR}"
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

# Copy Assets

if (EXISTS "${API_CLI_ASSETS_DIR}")

    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD

        COMMAND
            ${CMAKE_COMMAND}
            -E
            make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/public"

        COMMAND
            ${CMAKE_COMMAND}
            -E
            copy_directory
            "${API_CLI_ASSETS_DIR}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/public"

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

if (EXISTS "${API_CLI_ASSETS_DIR}")

    install(
        DIRECTORY
            "${API_CLI_ASSETS_DIR}/"
        DESTINATION
            "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/public"
    )
endif()

# Final configuration summary

message(STATUS "")
message_box(
    "API-cli configuration"
    "--DIVIDER--"
    "Project: ${PROJECT_NAME}"
    "Platform: ${CMAKE_SYSTEM_NAME}"
    "Compiler: ${CMAKE_CXX_COMPILER_ID}"
    "C++ standard: C++${CMAKE_CXX_STANDARD}"
    "Lua: ${API_CLI_LUA_RELEASE}"
    "Boost static: ${API_CLI_USE_STATIC_BOOST}"
    "Modules: ${MODULE_LIBRARIES}"
    "Docs: ${API_CLI_BUILD_DOCS}"
)
message(STATUS "")
