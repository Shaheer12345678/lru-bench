include(FetchContent)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "Benchmarks are being built with CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'; "
                    "numbers are only meaningful from a Release build")
endif()

FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.5
    GIT_SHALLOW TRUE
    SYSTEM)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
# Third-party code is not held to this repo's -Werror; its warnings are its maintainers' to fix.
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

function(lru_add_benchmark name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE lru::lru lru_warnings benchmark::benchmark)
endfunction()

lru_add_benchmark(bench_single_thread bench_single_thread.cpp)
lru_add_benchmark(bench_multi_thread bench_multi_thread.cpp)

# Plain executables: neither is timed, so neither needs Google Benchmark.
add_executable(hit_rate_sweep hit_rate_sweep.cpp)
target_link_libraries(hit_rate_sweep PRIVATE lru::lru lru_warnings)

add_executable(memory_footprint memory_footprint.cpp)
target_link_libraries(memory_footprint PRIVATE lru::lru lru_warnings)
