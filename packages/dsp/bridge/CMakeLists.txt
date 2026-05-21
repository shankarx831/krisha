# Bridge library CMake configuration
cmake_minimum_required(VERSION 3.20)

# Bridge library (ObjC++)
add_library(krisha_dsp_bridge
    KrishaDSPEngine.mm
)

target_include_directories(krisha_dsp_bridge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(krisha_dsp_bridge
    PUBLIC
        krisha_dsp
)

# Enable Objective-C++ and ARC
set_target_properties(krisha_dsp_bridge PROPERTIES
    COMPILE_FLAGS "-x objective-c++ -fobjc-arc"
    LINKER_LANGUAGE CXX
)

# Link Foundation framework
target_link_libraries(krisha_dsp_bridge
    PUBLIC
        "-framework Foundation"
)

# Install headers
install(FILES
    KrishaDSPEngine.h
    DESTINATION include/krisha/bridge
)

# Install library
install(TARGETS krisha_dsp_bridge
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
