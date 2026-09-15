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
