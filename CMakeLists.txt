cmake_minimum_required(VERSION 3.25)

project(lru_bench
    VERSION 0.1.0
    DESCRIPTION "Three LRU cache designs sharing one correctness suite, benchmarked against each other"
    LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

option(LRU_BUILD_TESTS "Build the correctness test suite" ${PROJECT_IS_TOP_LEVEL})
option(LRU_BUILD_BENCH "Build the Google Benchmark harness" OFF)

# Fail at configure time rather than silently producing a build with no benchmarks in it.
if(LRU_BUILD_BENCH)
    message(FATAL_ERROR "LRU_BUILD_BENCH was requested, but the benchmark harness has not been added yet")
endif()

add_library(lru INTERFACE)
add_library(lru::lru ALIAS lru)
target_include_directories(lru INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_compile_features(lru INTERFACE cxx_std_20)

# Warnings-as-errors applies to this repo's own targets only. Putting it on the library target
# would push -Werror onto anyone who consumes the headers with a different compiler version.
add_library(lru_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lru_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion)
endif()

if(LRU_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
