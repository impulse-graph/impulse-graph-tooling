cmake_minimum_required(VERSION 3.20)
project(impulse_opt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    include
    ../../impulse-graph/impulse-cpp/include
)

find_library(IMPULSE_GRAPH_LIB impulse_graph HINTS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../impulse-graph/impulse-cpp/build
    ${CMAKE_CURRENT_SOURCE_DIR}/../../impulse-graph/impulse-cpp
)

add_executable(impulse-opt
    src/main.cpp
    src/optimizer.cpp
    src/codecs.cpp
)

if (IMPULSE_GRAPH_LIB)
    message(STATUS "Linking impulse-opt against dynamic library: ${IMPULSE_GRAPH_LIB}")
    target_link_libraries(impulse-opt PRIVATE ${IMPULSE_GRAPH_LIB})
    set_target_properties(impulse-opt PROPERTIES BUILD_RPATH "@executable_path/../../impulse-graph/impulse-cpp")
else()
    message(STATUS "libimpulse_graph not found; compiling impulse_graph.cpp in-tree")
    target_sources(impulse-opt PRIVATE ../../impulse-graph/impulse-cpp/src/impulse_graph.cpp)
endif()
