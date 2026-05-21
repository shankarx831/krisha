cmake_minimum_required(VERSION 3.20)

# Enable testing
enable_testing()

# Test executable
add_executable(krisha_dsp_tests
    test_main.cpp
    test_biquad.cpp
    test_smoothing.cpp
    test_preset.cpp
    test_engine.cpp
    test_frequency_response.cpp
    test_parser.cpp
)

# Link against DSP library
target_link_libraries(krisha_dsp_tests PRIVATE krisha_dsp)

# Include test utilities
target_include_directories(krisha_dsp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# Compiler flags for tests
if(MSVC)
    target_compile_options(krisha_dsp_tests PRIVATE /W4)
else()
    target_compile_options(krisha_dsp_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Add test to CTest
add_test(NAME krisha_dsp_tests COMMAND krisha_dsp_tests)

# Set test properties
set_tests_properties(krisha_dsp_tests PROPERTIES
    TIMEOUT 60
    LABELS "unit"
)

message(STATUS "Test suite configured: krisha_dsp_tests")
