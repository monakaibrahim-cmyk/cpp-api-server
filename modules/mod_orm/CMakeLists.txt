cmake_minimum_required(VERSION 3.25)

set(MOD_NAME mod_orm)

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
)

target_link_libraries(${MOD_NAME} PUBLIC
    Crow::Crow
    lua_lib
)
