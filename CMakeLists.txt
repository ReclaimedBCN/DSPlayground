cmake_minimum_required(VERSION 3.10)

set(projectName "dspPlayground")
project(${projectName})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug) # DEBUG
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # Enable PIC for shared libs

# Includes
include_directories(${CMAKE_SOURCE_DIR}/includes)

# -- Host executable ----------------
add_executable(${projectName}
    host.cpp
    wavParser.cpp
)

# -- DSP plugin ---------------------
add_library(plugin SHARED plugin.cpp)

set_target_properties(plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    OUTPUT_NAME "plugin"
)

# -- RT Audio ------------------------
find_library(RTAUDIO_LIBRARY rtaudio)

target_link_libraries(${projectName}
    ${RTAUDIO_LIBRARY}
    "-framework CoreAudio"
    "-framework CoreFoundation"
    "-framework AudioUnit"
)

# -- FTXUI ------------------------
# add_subdirectory(FTXUI) # Use local FTXUI subdirectory
find_package(ftxui REQUIRED) # or if ftxui installed system-wide

target_link_libraries(${projectName}
  PRIVATE ftxui::screen
  PRIVATE ftxui::dom
  PRIVATE ftxui::component
)

# for linter to find FTXUI library files
target_include_directories(${projectName} PRIVATE
    ${CMAKE_SOURCE_DIR}/FTXUI/include
)

