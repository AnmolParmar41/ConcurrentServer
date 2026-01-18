cmake_minimum_required(VERSION 3.10)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(ConcurrentServer)

set(CMAKE_CXX_STANDARD 11)

add_library(utils STATIC lib/utils.c)

target_include_directories(utils PUBLIC include)

add_executable(uv-lib src/uv-lib.c)

target_link_libraries(uv-lib PRIVATE pthread utils uv)
