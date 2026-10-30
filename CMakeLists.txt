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

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/IEMPluginSuite/MultiEncoder/Source/PluginProcessor.cpp)
    message(FATAL_ERROR "third_party/IEMPluginSuite is not initialised: git submodule update --init third_party/IEMPluginSuite (not --recursive)")
endif()

# JUCE's way for module code outside an app: a library of its own that carries
# the modules and their definitions, as StemDeck's tests do. The modules are
# PRIVATE: a juce:: target hands its module .cpp files to every target that
# links it publicly, so each consumer would compile its own copy of JUCE.
add_library(a3-engine-juce STATIC)
target_link_libraries(a3-engine-juce
    PRIVATE juce::juce_audio_basics juce::juce_core juce::juce_osc juce::juce_recommended_config_flags
    juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp juce::juce_opengl
    juce::juce_audio_devices juce::juce_audio_utils juce::juce_gui_basics juce::juce_gui_extra
    juce::juce_cryptography)
# JUCE_MODAL_LOOPS_PERMITTED is IEM's global definition; it changes module code,
# so it has to be on the library that compiles the modules.
target_compile_definitions(a3-engine-juce PUBLIC JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
# The device backend is ALSA; JUCE's JACK device type is never compiled (the JACK backend is our own).
target_compile_definitions(a3-engine-juce PUBLIC JUCE_JACK=0 JUCE_ALSA=1)
target_compile_definitions(a3-engine-juce INTERFACE $<TARGET_PROPERTY:a3-engine-juce,COMPILE_DEFINITIONS>)
target_include_directories(a3-engine-juce INTERFACE $<TARGET_PROPERTY:a3-engine-juce,INCLUDE_DIRECTORIES>)

include(cmake/A3IemNodes.cmake)
include(third_party/IEMPluginSuite/Versions.cmake)   # only set()s the plug-in versions
a3_add_iem_nodes("" a3-engine-juce)

# The delay page's values as DualDelay's parameters: plain C++, no JUCE and no engine headers, so
# Core (the beat it announces, R16), the engine and the patched DualDelay share one rule.
add_library(a3-delay STATIC src/a3-delay/DelayParameters.cpp)
target_include_directories(a3-delay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(a3-delay PRIVATE -Wall -Wextra -Werror=switch)

# The one clock (src/a3-clock/ClockSteering.h): steering the Core's PTP hardware clock from the
# Scarlett. The loop and the measurements are plain C++; the status file and the daemon use juce_core.
add_library(a3-clock STATIC src/a3-clock/ClockSteering.cpp src/a3-clock/ClockMeasure.cpp
    src/a3-clock/ClockStatus.cpp src/a3-clock/ClockDaemon.cpp src/a3-clock/ClockPtpState.cpp)
target_include_directories(a3-clock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/a3-clock)
target_link_libraries(a3-clock PUBLIC a3-engine-juce)
target_compile_options(a3-clock PRIVATE -Wall -Wextra -Werror=switch)

add_library(a3-engine STATIC src/a3-engine/Laws.cpp src/a3-engine/PurestGain.cpp
    src/a3-engine/SmoothEQ3.cpp src/a3-engine/Isolator3.cpp src/a3-engine/Meters.cpp src/a3-engine/DjFilter.cpp
    src/a3-engine/ChannelStrip.cpp src/a3-engine/DeckStrip.cpp src/a3-engine/ChannelSelect.cpp src/a3-engine/SpatialConfig.cpp src/a3-engine/SpatialBus.cpp src/a3-engine/AmbiEncoderBus.cpp src/a3-engine/EngineConfig.cpp
    src/a3-engine/DelayModulation.cpp src/a3-engine/EngineStages.cpp src/a3-engine/OutputGate.cpp src/a3-engine/Engine.cpp
    src/a3-engine/VuTaps.cpp src/a3-engine/VuBundles.cpp src/a3-engine/DeckLetters.cpp src/a3-engine/DecoderSummary.cpp)
target_include_directories(a3-engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/a3-engine)
target_link_libraries(a3-engine PUBLIC a3-engine-juce a3-iem a3-delay)
target_compile_options(a3-engine PRIVATE -Wall -Wextra -Werror=switch)

# The link planning and the JACK linker: juce_core and libjack only, no engine headers, so another
# JACK client (StemDeck) can compile the same three files.
find_package(PkgConfig REQUIRED)
pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)
add_library(a3-links STATIC src/a3-links/LinkPlan.cpp src/a3-links/LinkMap.cpp src/a3-links/JackLinker.cpp)
target_include_directories(a3-links PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(a3-links PUBLIC a3-engine-juce PkgConfig::JACK)
target_compile_options(a3-links PRIVATE -Wall -Wextra -Werror=switch)

add_subdirectory(bench)
add_subdirectory(app)
add_subdirectory(src/a3-core)

option(A3_ENGINE_TESTS "Build the engine tests" ON)
if(A3_ENGINE_TESTS)
    enable_testing()
    add_subdirectory(tests/engine)
    add_subdirectory(tests/core)
endif()

# Opt-in: null tests of our compiled IEM nodes against the IEM VST3s installed on
# the machine. Needs its own JUCE module library, built with VST3 hosting, and a
# second set of IEM libraries on top of it (the suffix keeps the targets apart).
option(A3_ENGINE_IEM_NULLTESTS "Build the IEM-vs-installed-VST3 null tests (not part of ctest)" OFF)
if(A3_ENGINE_IEM_NULLTESTS)
    add_library(a3-engine-juce-vst3host STATIC)
    target_link_libraries(a3-engine-juce-vst3host
        PRIVATE juce::juce_audio_basics juce::juce_core juce::juce_osc juce::juce_recommended_config_flags
        juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp juce::juce_opengl)
    target_compile_definitions(a3-engine-juce-vst3host PUBLIC JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_PLUGINHOST_VST3=1)
    target_compile_definitions(a3-engine-juce-vst3host INTERFACE $<TARGET_PROPERTY:a3-engine-juce-vst3host,COMPILE_DEFINITIONS>)
    target_include_directories(a3-engine-juce-vst3host INTERFACE $<TARGET_PROPERTY:a3-engine-juce-vst3host,INCLUDE_DIRECTORIES>)
    a3_add_iem_nodes("-vst3host" a3-engine-juce-vst3host)
    add_subdirectory(tests/iem-null)
endif()
