cmake_minimum_required(VERSION 3.16)
project(checker_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(demo_points INTERFACE)
target_include_directories(demo_points INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(demo_points::demo_points ALIAS demo_points)

if(NOT TARGET checker::checker)
    set(CHECKER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../checker
        ${CMAKE_CURRENT_BINARY_DIR}/checker)
endif()

checker_add_stress_test(demo
    GENERATORS gen_points
    SOURCES    src/main.cpp
    GENERATOR_DIR src
    SCRIPT     tests/gen.script
    CASES_DIR  tests/cases
    LINK       demo_points::demo_points)
