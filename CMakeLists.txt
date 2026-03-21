cmake_minimum_required(VERSION 3.20.0)

project(spmc LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(src)
