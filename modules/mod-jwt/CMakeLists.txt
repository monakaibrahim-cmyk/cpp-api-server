cmake_minimum_required(VERSION 3.25)

set(MOD_NAME mod-jwt)

find_package(OpenSSL REQUIRED)

file(GLOB_RECURSE MOD_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

add_library(${MOD_NAME} STATIC
    ${MOD_SOURCES}
)

target_include_directories(${MOD_NAME} PUBLIC
    include
    ${CMAKE_SOURCE_DIR}/include
    ${sol2_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(${MOD_NAME} PUBLIC
    Crow::Crow
    lua_lib
    OpenSSL::Crypto
)
