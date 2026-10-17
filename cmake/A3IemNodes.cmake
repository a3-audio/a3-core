# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles IEM plug-ins from third_party/IEMPluginSuite, unchanged, into
# libraries a3::IemNode wraps. Called once per JUCE build: every target carries
# SUFFIX, and JUCE comes only from JUCE_TARGET, so a second call with another
# JUCE library (Task 4's VST3-host build) does not collide with the first.

set(A3_IEM_DIR ${CMAKE_CURRENT_LIST_DIR}/../third_party/IEMPluginSuite)
get_filename_component(A3_IEM_DIR ${A3_IEM_DIR} ABSOLUTE)
set(A3_IEM_ADAPTER_DIR ${CMAKE_CURRENT_LIST_DIR}/../src/iem-adapter/JuceLibraryCode)
get_filename_component(A3_IEM_ADAPTER_DIR ${A3_IEM_ADAPTER_DIR} ABSOLUTE)
set(A3_IEM_NODES_DIR ${CMAKE_CURRENT_LIST_DIR}/../src/iem-nodes)
get_filename_component(A3_IEM_NODES_DIR ${A3_IEM_NODES_DIR} ABSOLUTE)
set(A3_IEM_PUBLIC_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/../src/a3-engine)
get_filename_component(A3_IEM_PUBLIC_INCLUDE_DIR ${A3_IEM_PUBLIC_INCLUDE_DIR} ABSOLUTE)

# What every IEM translation unit sees, as IEM's own top-level CMakeLists sets it.
function(a3_iem_use_iem_environment TARGET SUFFIX JUCE_TARGET)
    target_include_directories(${TARGET} PRIVATE ${A3_IEM_ADAPTER_DIR} ${A3_IEM_DIR}/resources)
    target_compile_definitions(${TARGET} PRIVATE DONT_SET_USING_JUCE_NAMESPACE=1)
    target_link_libraries(${TARGET} PRIVATE ${JUCE_TARGET} a3-iem-fonts${SUFFIX})
endfunction()

# One IEM plug-in: its processor and editor (createEditor() names the editor,
# so it has to link), with the plug-in's own name and version in every TU that
# includes its PluginProcessor.h: AudioProcessorBase names the APVTS state after
# JucePlugin_Name, and setStateInformation checks that tag.
function(a3_iem_add_plugin SUFFIX JUCE_TARGET PLUGIN NODE_SOURCE)
    set(source_dir ${A3_IEM_DIR}/${PLUGIN}/Source)
    set(iem_sources ${source_dir}/PluginProcessor.cpp ${source_dir}/PluginEditor.cpp)
    set(target a3-iem-${PLUGIN}${SUFFIX})

    add_library(${target} STATIC ${iem_sources} ${NODE_SOURCE})
    a3_iem_use_iem_environment(${target} "${SUFFIX}" ${JUCE_TARGET})
    target_include_directories(${target} PRIVATE ${source_dir} ${A3_IEM_NODES_DIR} ${A3_IEM_PUBLIC_INCLUDE_DIR})
    target_link_libraries(${target} PRIVATE a3-iem-common${SUFFIX})
    target_compile_definitions(${target} PRIVATE
        JucePlugin_Name="${PLUGIN}"
        JucePlugin_VersionString="${${PLUGIN}_VERSION}"
        createPluginFilter=createPluginFilter_${PLUGIN})
    set_source_files_properties(${iem_sources} TARGET_DIRECTORY ${target}
        PROPERTIES COMPILE_OPTIONS -w)   # upstream code, unchanged
    set(${PLUGIN}_TARGET ${target} PARENT_SCOPE)
endfunction()

function(a3_add_iem_nodes SUFFIX JUCE_TARGET)
    juce_add_binary_data(a3-iem-fonts${SUFFIX} SOURCES
        ${A3_IEM_DIR}/resources/lookAndFeel/Roboto-Bold.ttf
        ${A3_IEM_DIR}/resources/lookAndFeel/Roboto-Light.ttf
        ${A3_IEM_DIR}/resources/lookAndFeel/Roboto-Medium.ttf
        ${A3_IEM_DIR}/resources/lookAndFeel/Roboto-Regular.ttf)

    # JucePlugin_Name is only IEM's OSC address prefix here, and the receiver stays closed.
    add_library(a3-iem-common${SUFFIX} STATIC
        ${A3_IEM_DIR}/resources/OSC/OSCParameterInterface.cpp
        ${A3_IEM_DIR}/resources/OSC/OSCStatus.cpp
        ${A3_IEM_DIR}/resources/efficientSHvanilla.cpp)
    a3_iem_use_iem_environment(a3-iem-common${SUFFIX} "${SUFFIX}" ${JUCE_TARGET})
    target_compile_definitions(a3-iem-common${SUFFIX} PRIVATE JucePlugin_Name="IEM")
    target_compile_options(a3-iem-common${SUFFIX} PRIVATE -w)   # upstream code, unchanged

    # MultiEncoder's and SimpleDecoder's presets are both `namespace Presets`
    # in a Presets.h with different data; each gets its own namespace.
    juce_add_binary_data(a3-iem-MultiEncoderPresets${SUFFIX}
        HEADER_NAME Presets.h
        NAMESPACE MultiEncoderPresets
        SOURCES
        ${A3_IEM_DIR}/_configurationFiles/t_design_12ch.json
        ${A3_IEM_DIR}/_configurationFiles/t_design_24ch.json
        ${A3_IEM_DIR}/_configurationFiles/t_design_36ch.json
        ${A3_IEM_DIR}/_configurationFiles/t_design_48ch.json
        ${A3_IEM_DIR}/_configurationFiles/t_design_60ch.json)
    a3_iem_add_plugin("${SUFFIX}" ${JUCE_TARGET} MultiEncoder ${A3_IEM_NODES_DIR}/MultiEncoderNode.cpp)
    target_link_libraries(${MultiEncoder_TARGET} PRIVATE a3-iem-MultiEncoderPresets${SUFFIX})
    set_property(SOURCE ${A3_IEM_DIR}/MultiEncoder/Source/PluginProcessor.cpp
        TARGET_DIRECTORY ${MultiEncoder_TARGET}
        APPEND PROPERTY COMPILE_DEFINITIONS Presets=MultiEncoderPresets)

    add_library(a3-iem${SUFFIX} STATIC ${A3_IEM_NODES_DIR}/IemNode.cpp)
    target_include_directories(a3-iem${SUFFIX} PUBLIC ${A3_IEM_PUBLIC_INCLUDE_DIR})
    target_include_directories(a3-iem${SUFFIX} PRIVATE ${A3_IEM_NODES_DIR})
    target_link_libraries(a3-iem${SUFFIX} PUBLIC ${JUCE_TARGET} PRIVATE ${MultiEncoder_TARGET})
    target_compile_options(a3-iem${SUFFIX} PRIVATE -Wall -Wextra -Werror=switch)
endfunction()
