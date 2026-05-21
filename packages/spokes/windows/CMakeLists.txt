cmake_minimum_required(VERSION 3.20)
project(radioform_apo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build as a shared library to mimic a Windows DLL
add_library(radioform_apo SHARED KrishaAPO.cpp)

# Include the mock COM headers and the DSP core public headers
target_include_directories(radioform_apo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mock
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/include
)

# Link against the universal static DSP library built in Phase 1
target_link_libraries(radioform_apo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/build/libradioform_dsp.a
)
