cmake_minimum_required(VERSION 3.10)
project(dspPlayground)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable PIC for shared libs
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Includes
include_directories(${CMAKE_SOURCE_DIR}/includes)

# -----------------------
# Host executable
# -----------------------
add_executable(dspPlayground host.cpp)

find_library(RTAUDIO_LIBRARY rtaudio)

target_link_libraries(dspPlayground
    ${RTAUDIO_LIBRARY}
    "-framework CoreAudio"
    "-framework CoreFoundation"
    "-framework AudioUnit"
)

# -----------------------
# DSP plugin (hot reload)
# -----------------------
add_library(dsp SHARED dsp.cpp)

set_target_properties(dsp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    OUTPUT_NAME "dsp"
)

