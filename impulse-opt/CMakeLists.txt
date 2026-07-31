cmake_minimum_required(VERSION 3.20)
project(impulse_opt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    include
    ../../impulse-graph/impulse-cpp/include
)

add_executable(impulse-opt
    src/main.cpp
    src/optimizer.cpp
    src/codecs.cpp
    ../../impulse-graph/impulse-cpp/src/impulse_graph.cpp
)
