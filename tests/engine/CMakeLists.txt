# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
find_package(GTest REQUIRED)

file(GLOB REFERENCE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/reference/airwindows/*/*.cpp)
# Every plugin defines the VST entry point of the same name; rename it per
# plugin (in the compile, not in the unchanged sources) so they can share a binary.
foreach(src IN LISTS REFERENCE_SOURCES)
    get_filename_component(plugin "${src}" NAME_WE)
    set_source_files_properties("${src}" PROPERTIES
        COMPILE_DEFINITIONS "createEffectInstance=createEffectInstance_${plugin}")
endforeach()
add_library(a3-engine-reference STATIC
    reference/PurestGainReference.cpp reference/SmoothEQ3Reference.cpp
    reference/Isolator3Reference.cpp ${REFERENCE_SOURCES})
target_include_directories(a3-engine-reference PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/reference
    ${CMAKE_CURRENT_SOURCE_DIR}/reference/airwindows
    ${CMAKE_CURRENT_SOURCE_DIR}/reference/shim)
target_compile_options(a3-engine-reference PRIVATE -w)   # upstream code, unchanged

add_executable(a3-engine-tests ReferenceSmoke.cpp Laws.cpp PurestGain.cpp SmoothEQ3.cpp
    Isolator3.cpp Meters.cpp DjFilter.cpp ChannelStrip.cpp DeckStrip.cpp StripTiming.cpp Bench.cpp
    JuceEnvironment.cpp IemCompileTrial.cpp IemNode.cpp Layout.cpp SpatialBus.cpp SpatialTiming.cpp
    SpatialAllocation.cpp)
target_include_directories(a3-engine-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(a3-engine-tests PRIVATE a3-engine a3-engine-bench-lib a3-engine-reference
    GTest::gtest GTest::gtest_main)
target_compile_definitions(a3-engine-tests PRIVATE
    A3_CURVES_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/data/curves-golden.json"
    A3_IEM_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/IEMPluginSuite/SimpleDecoder/Source/Presets"
    A3_SPATIAL_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../../data/spatial"
    A3_SPATIAL_REAPER_BOOTH_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../../data/spatial-reaper-booth-6.1"
    A3_TRUTH_COPY="${CMAKE_CURRENT_SOURCE_DIR}/data/a3-osc.json")
include(GoogleTest)
gtest_discover_tests(a3-engine-tests)
