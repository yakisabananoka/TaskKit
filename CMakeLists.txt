cmake_minimum_required(VERSION 3.15)

project(TaskKit VERSION 0.1.0 LANGUAGES CXX)

# C++標準の設定
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ビルドタイプのデフォルト設定
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# コンパイラ警告の設定
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# インクルードディレクトリ
include_directories(${CMAKE_SOURCE_DIR}/include)

# サブディレクトリの追加
add_subdirectory(include)

# サンプルとテストはオプションで有効化
option(BUILD_SAMPLES "Build sample programs" ON)
option(BUILD_TESTS "Build tests" ON)

if(BUILD_SAMPLES)
    add_subdirectory(sample)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
