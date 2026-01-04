cmake_minimum_required(VERSION 3.15)

project(TaskKit VERSION 0.1.0 LANGUAGES CXX)

# Parallel build settings
include(ProcessorCount)
ProcessorCount(N)
if(NOT N EQUAL 0)
    set(CMAKE_BUILD_PARALLEL_LEVEL ${N} CACHE STRING "Number of parallel build jobs")
    message(STATUS "Parallel build enabled with ${N} jobs")
endif()

# C++ standard settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler warning settings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Add subdirectories
add_subdirectory(include)

# Optional build for samples and tests
option(BUILD_SAMPLES "Build sample programs" ON)
option(BUILD_TESTS "Build tests" ON)

if(BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
