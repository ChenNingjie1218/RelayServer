cmake_minimum_required(VERSION 3.16.3)
project(RelayServer)

# # 设置所需的编译器
# set(CMAKE_CXX_COMPILER g++)  # 将编译器设置为 g++

# debug
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -std=c++20")
set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)



