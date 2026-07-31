cmake_minimum_required(VERSION 3.20)
project(impulse_compile LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(../../../impulse-graph/impulse-cpp/include)

add_executable(impulse-compile main.cpp)
