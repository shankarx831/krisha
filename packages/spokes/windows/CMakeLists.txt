cmake_minimum_required(VERSION 3.20)
project(krisha_apo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build as a shared library to mimic a Windows DLL
add_library(krisha_apo SHARED KrishaAPO.cpp)

# Include the mock COM headers and the DSP core public headers
target_include_directories(krisha_apo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mock
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/include
)

# Link against the universal static DSP library built in Phase 1
if(MSVC)
    # Find the MSVC .lib file (checking both Release and Debug folders just in case)
    find_library(KRISHA_DSP_LIB NAMES krisha_dsp PATHS 
        "${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/build/Release"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/build/Debug"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/build"
    )
else()
    set(KRISHA_DSP_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/build/libkrisha_dsp.a")
endif()

target_link_libraries(krisha_apo PRIVATE
    ${KRISHA_DSP_LIB}
)

