cmake_minimum_required(VERSION 3.20)
project(krisha_jni LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build JNI bridge as a shared library
add_library(krisha_jni SHARED jni_bridge.cpp)

# Include the mock jni.h and the DSP core public headers
target_include_directories(krisha_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mock
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/include
)

# Link against the universal static DSP library built in Phase 1
target_link_libraries(krisha_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/build/libkrisha_dsp.a
)
