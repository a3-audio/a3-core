# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.22)
project(A3Engine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND}" -E env
        "CCACHE_BASEDIR=${CMAKE_SOURCE_DIR}" "CCACHE_NOHASHDIR=true" "${CCACHE_PROGRAM}")
endif()

find_package(JUCE CONFIG REQUIRED)

# JUCE's way for module code outside an app: a library of its own that carries
# the modules and their definitions, as StemDeck's tests do.
add_library(a3-engine-juce STATIC)
target_link_libraries(a3-engine-juce
    PUBLIC juce::juce_audio_basics juce::juce_core juce::juce_recommended_config_flags)
target_compile_definitions(a3-engine-juce PUBLIC JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_compile_definitions(a3-engine-juce INTERFACE $<TARGET_PROPERTY:a3-engine-juce,COMPILE_DEFINITIONS>)
target_include_directories(a3-engine-juce INTERFACE $<TARGET_PROPERTY:a3-engine-juce,INCLUDE_DIRECTORIES>)

add_library(a3-engine STATIC src/a3-engine/Laws.cpp src/a3-engine/PurestGain.cpp
    src/a3-engine/SmoothEQ3.cpp src/a3-engine/Isolator3.cpp src/a3-engine/Meters.cpp
    src/a3-engine/ChannelStrip.cpp)
target_include_directories(a3-engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/a3-engine)
target_link_libraries(a3-engine PUBLIC a3-engine-juce)
target_compile_options(a3-engine PRIVATE -Wall -Wextra -Werror=switch)

option(A3_ENGINE_TESTS "Build the engine tests" ON)
if(A3_ENGINE_TESTS)
    enable_testing()
    add_subdirectory(tests/engine)
endif()
