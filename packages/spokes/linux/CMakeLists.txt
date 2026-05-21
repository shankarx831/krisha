cmake_minimum_required(VERSION 3.20)
project(radioform_pw_sink LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compile the daemon executable
add_executable(radioform_pw_sink main.c mock/pipewire_mock.c)

# Include the mock PipeWire/SPA headers and the DSP core public headers
target_include_directories(radioform_pw_sink PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mock
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/include
)

# Link against the universal static DSP library built in Phase 1
target_link_libraries(radioform_pw_sink PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/build/libradioform_dsp.a
)

# Force the C++ linker to resolve C++ symbols inside the static library cleanly
set_target_properties(radioform_pw_sink PROPERTIES LINKER_LANGUAGE CXX)
