# ---------------------------------------------------------------------------------------
# ecomm unit tests
# ---------------------------------------------------------------------------------------
# Each test file becomes a standalone gtest executable registered with CTest.
# Tests link against the header-only `ecomm` interface target, which pulls in etools/eser
# via transitive dependency.
# ---------------------------------------------------------------------------------------

include(GoogleTest)

function(ecomm_add_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE ecomm GTest::gtest_main)
    target_compile_features(${target} PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    gtest_discover_tests(${target})
endfunction()

ecomm_add_test(test_packet_header protocol/test_packet_header.cpp)
ecomm_add_test(test_packet        protocol/test_packet.cpp)
ecomm_add_test(test_validator     protocol/test_validator.cpp)
ecomm_add_test(test_error         protocol/test_error.cpp)
# Override the default max message length (65535) to a value that fits
# comfortably inside a 64-byte test packet (payload = 63 bytes, prefix = 4).
target_compile_definitions(test_error PRIVATE ECOMM_MAX_ERROR_MESSAGE_LENGTH=55)
