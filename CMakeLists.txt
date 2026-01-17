cmake_minimum_required(VERSION 3.16)

set(projectName "DSPlayground")
project(${projectName})

option(USE_SYSTEM_RTAUDIO "Use system-wide install of rtaudio" ON)
option(USE_SYSTEM_FTXUI "Use system-wide install of FTXUI" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# set(CMAKE_BUILD_TYPE Debug) # DEBUG
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # Enable PIC for shared libs

# -- Host executables ----------------
add_executable(${projectName}
    host.cpp
    wavParser.cpp
    ui.cpp
)

# -- DSP plugin ---------------------
add_library(plugin SHARED plugin.cpp)

set_target_properties(plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    OUTPUT_NAME "plugin"
)

# -- Libraries ---------------------
target_include_directories(${projectName} PRIVATE
    external/rtaudio
    external/FTXUI/include
)

if(USE_SYSTEM_RTAUDIO)
    find_library(RTAUDIO_LIBRARY rtaudio) # Use system-wide rtaudio installation 
else()
    add_subdirectory(external/rtaudio) # Use local rtaudio subdirectory
endif()

if(USE_SYSTEM_RTAUDIO)
    target_link_libraries(${projectName}
        # FTXUI
        PRIVATE ftxui::screen
        PRIVATE ftxui::dom
        PRIVATE ftxui::component
        # RT Audio
        ${RTAUDIO_LIBRARY}
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework AudioUnit"
    )
else()
    target_link_libraries(${projectName}
        # FTXUI
        PRIVATE ftxui::screen
        PRIVATE ftxui::dom
        PRIVATE ftxui::component
        # RT Audio
        PRIVATE rtaudio
    )
endif()

# disable FTXUI options
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_MODULES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS_FUZZER OFF CACHE BOOL "" FORCE)
set(FTXUI_CLANG_TIDY OFF CACHE BOOL "" FORCE)
set(FTXUI_DEV_WARNINGS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_COVERAGE OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FTXUI_QUIET OFF CACHE BOOL "" FORCE)

if(USE_SYSTEM_FTXUI)
    find_package(ftxui REQUIRED) # Use system-wide ftxui installation 
else()
    add_subdirectory(external/FTXUI) # Use local FTXUI subdirectory
endif()


