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
    # /utf-8: sources are UTF-8 without BOM (avoids C4819 on non-ASCII comments)
    # /wd4324: padding added for alignas is intentional (cache-line separation)
    add_compile_options(/W4 /utf-8 /wd4324)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Some MinGW distributions ship a libmingw32.a that requires the UCRT
# (__imp__Exit / __imp_quick_exit) while still linking msvcrt by default,
# which breaks linking anything that touches the quick_exit family (e.g.
# Google Test). Switch to the UCRT when the toolchain provides it.
if(MINGW)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LINK_OPTIONS -mcrtdll=ucrt)
    check_cxx_source_compiles("#include <cstdlib>\nint main() { _Exit(0); }" TASKKIT_MINGW_HAS_UCRT)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(TASKKIT_MINGW_HAS_UCRT)
        add_compile_options(-mcrtdll=ucrt)
        add_link_options(-mcrtdll=ucrt)
    endif()
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
