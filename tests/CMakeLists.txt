include(FetchContent)

# SYSTEM marks GoogleTest's headers as system headers, so the strict warning set applied to this
# repo's code is not tripped by third-party code it has no control over.
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
    SYSTEM)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Each implementation gets its own executable because the shared suite in test_correctness.hpp
# defines its test bodies in the header and so may only be compiled once per binary.
function(lru_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE lru::lru lru_warnings GTest::gtest_main)
    gtest_discover_tests(${name})
endfunction()

lru_add_test(lru_test_v1 test_lru_v1.cpp)
lru_add_test(lru_test_v2 test_lru_v2.cpp)
lru_add_test(lru_test_v3 test_lru_v3.cpp)

# Separate executable because it replaces the global operator new and delete for the whole process.
# Not built with a sanitizer: the sanitizer runtimes supply their own operator new, so under TSan the
# replacement does not link and under ASan it silently displaces ASan's new/delete checking. The
# allocation count is a property of the normal build, and the code it exercises still runs under every
# sanitizer through the other test executables.
if(NOT LRU_SANITIZER)
    lru_add_test(lru_test_allocations test_allocations.cpp)
endif()

# Sanitizer builds run many times slower than a normal build, so they pass a smaller count here
# instead of the test carrying a special case for them.
set(LRU_CONCURRENT_OPS 1000000 CACHE STRING "Total operations performed by the concurrency stress test")
if(NOT LRU_CONCURRENT_OPS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "LRU_CONCURRENT_OPS must be a positive integer, not '${LRU_CONCURRENT_OPS}'")
endif()

find_package(Threads REQUIRED)
lru_add_test(lru_test_concurrent test_concurrent.cpp)
target_link_libraries(lru_test_concurrent PRIVATE Threads::Threads)
target_compile_definitions(lru_test_concurrent PRIVATE LRU_CONCURRENT_OPS=${LRU_CONCURRENT_OPS})
