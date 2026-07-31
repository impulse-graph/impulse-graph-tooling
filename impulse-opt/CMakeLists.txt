cmake_minimum_required(VERSION 3.20)
project(impulse_opt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

add_executable(impulse-opt src/heavy_optimizer.cpp)
