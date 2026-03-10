cmake_minimum_required(VERSION 3.10)
project(Server)

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SRC_FILES "src/*.cpp")

add_executable(Server ${SRC_FILES})

include_directories(src)

find_package(Threads REQUIRED)
target_link_libraries(Server PRIVATE Threads::Threads)
