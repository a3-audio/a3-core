# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles IEM plug-ins from third_party/IEMPluginSuite into libraries a3::IemNode
# wraps, unchanged except DualDelay, which gets patches/iem/DualDelay-crossfade.patch
# on a copy in the build tree (R17; the submodule stays clean at its pin). Called once per JUCE build: every target carries
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

set(A3_IEM_PATCH_DIR ${CMAKE_CURRENT_LIST_DIR}/../patches/iem)
get_filename_component(A3_IEM_PATCH_DIR ${A3_IEM_PATCH_DIR} ABSOLUTE)
set(A3_SOURCE_ROOT ${CMAKE_CURRENT_LIST_DIR}/../src)
get_filename_component(A3_SOURCE_ROOT ${A3_SOURCE_ROOT} ABSOLUTE)

# A plug-in's Source with a3's patch applied, in the build tree: copied, patched in a stage, then
# copied on only where a file changed, so a reconfigure does not rebuild the plug-in. Configure
# fails, naming the patch, when it no longer applies (an IEM update moved the lines). The copy's
# "../../resources/..." includes resolve through the original Source directory on the include path.
function(a3_iem_patched_source PLUGIN PATCH_FILE OUT_DIR)
    find_program(A3_PATCH_PROGRAM patch REQUIRED)
    set(original ${A3_IEM_DIR}/${PLUGIN}/Source)
    set(stage ${CMAKE_BINARY_DIR}/iem-patch-stage/${PLUGIN})
    set(patched ${CMAKE_BINARY_DIR}/iem-patched/${PLUGIN}/Source)
    file(REMOVE_RECURSE ${stage})
    file(MAKE_DIRECTORY ${stage})
    file(COPY ${original}/ DESTINATION ${stage})
    execute_process(
        COMMAND ${A3_PATCH_PROGRAM} -p1 --forward --batch --fuzz=0 --no-backup-if-mismatch -d ${stage} -i ${PATCH_FILE}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${PATCH_FILE} does not apply to ${original} (IEM updated?):\n${output}")
    endif()
    file(GLOB_RECURSE staged RELATIVE ${stage} ${stage}/*)
    foreach(file IN LISTS staged)
        configure_file(${stage}/${file} ${patched}/${file} COPYONLY)
    endforeach()
    file(GLOB originals ${original}/*.cpp ${original}/*.h)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PATCH_FILE} ${originals})
    set(${OUT_DIR} ${patched} PARENT_SCOPE)
endfunction()

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
# An optional fifth argument is a patch file (see a3_iem_patched_source).
function(a3_iem_add_plugin SUFFIX JUCE_TARGET PLUGIN NODE_SOURCE)
    set(original_dir ${A3_IEM_DIR}/${PLUGIN}/Source)
    set(source_dir ${original_dir})
    if(ARGC GREATER 4)
        a3_iem_patched_source(${PLUGIN} ${ARGV4} source_dir)
    endif()
    set(iem_sources ${source_dir}/PluginProcessor.cpp ${source_dir}/PluginEditor.cpp)
    set(target a3-iem-${PLUGIN}${SUFFIX})

    add_library(${target} STATIC ${iem_sources} ${NODE_SOURCE})
    a3_iem_use_iem_environment(${target} "${SUFFIX}" ${JUCE_TARGET})
    # The (patched) Source first, so the node source includes its PluginProcessor.h; the original
    # after it, for the copy's relative includes; src/ for "a3-delay/DelayParameters.h".
    target_include_directories(${target} PRIVATE ${source_dir} ${original_dir} ${A3_IEM_NODES_DIR}
        ${A3_IEM_PUBLIC_INCLUDE_DIR} ${A3_SOURCE_ROOT})
    target_link_libraries(${target} PRIVATE a3-iem-common${SUFFIX})
    target_compile_definitions(${target} PRIVATE
        JucePlugin_Name="${PLUGIN}"
        JucePlugin_VersionString="${${PLUGIN}_VERSION}"
        createPluginFilter=createPluginFilter_${PLUGIN})
    set_source_files_properties(${iem_sources} TARGET_DIRECTORY ${target}
        PROPERTIES COMPILE_OPTIONS -w)   # upstream code
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

    juce_add_binary_data(a3-iem-SimpleDecoderPresets${SUFFIX}
        HEADER_NAME Presets.h
        NAMESPACE Presets
        SOURCES
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/5point1.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/5point1point4.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/7point1.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/7point1point4.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/22_2_NHK.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/Cube_8ch.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/CUBE.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/MSDecoder.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/Produktionsstudio.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/Quadraphonic.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/t_design_12ch.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/t_design_24ch.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/t_design_36ch.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/t_design_48ch.json
        ${A3_IEM_DIR}/SimpleDecoder/Source/Presets/t_design_60ch.json)
    a3_iem_add_plugin("${SUFFIX}" ${JUCE_TARGET} SimpleDecoder ${A3_IEM_NODES_DIR}/SimpleDecoderNode.cpp)
    target_link_libraries(${SimpleDecoder_TARGET} PRIVATE a3-iem-SimpleDecoderPresets${SUFFIX})

    # The IEM sources include these as <IRData.h> and <EQData.h>.
    file(GLOB A3_IEM_BINAURAL_EQS ${A3_IEM_DIR}/BinauralDecoder/Source/EQ/*.wav)
    list(SORT A3_IEM_BINAURAL_EQS)
    juce_add_binary_data(a3-iem-BinauralEQs${SUFFIX}
        HEADER_NAME EQData.h
        NAMESPACE EQData
        SOURCES ${A3_IEM_BINAURAL_EQS})
    juce_add_binary_data(a3-iem-BinauralIRs${SUFFIX}
        HEADER_NAME IRData.h
        NAMESPACE IRData
        SOURCES
        ${A3_IEM_DIR}/BinauralDecoder/Source/IRs/irsOrd1.wav
        ${A3_IEM_DIR}/BinauralDecoder/Source/IRs/irsOrd2.wav
        ${A3_IEM_DIR}/BinauralDecoder/Source/IRs/irsOrd3.wav
        ${A3_IEM_DIR}/BinauralDecoder/Source/IRs/irsOrd4.wav
        ${A3_IEM_DIR}/BinauralDecoder/Source/IRs/irsOrd5.wav
        ${A3_IEM_DIR}/BinauralDecoder/Source/IRs/irsOrd6.wav
        ${A3_IEM_DIR}/BinauralDecoder/Source/IRs/irsOrd7.wav)
    a3_iem_add_plugin("${SUFFIX}" ${JUCE_TARGET} BinauralDecoder ${A3_IEM_NODES_DIR}/BinauralDecoderNode.cpp)
    target_link_libraries(${BinauralDecoder_TARGET} PRIVATE
        a3-iem-BinauralEQs${SUFFIX} a3-iem-BinauralIRs${SUFFIX})

    a3_iem_add_plugin("${SUFFIX}" ${JUCE_TARGET} DualDelay ${A3_IEM_NODES_DIR}/DualDelayNode.cpp
        ${A3_IEM_PATCH_DIR}/DualDelay-crossfade.patch)
    a3_iem_add_plugin("${SUFFIX}" ${JUCE_TARGET} StereoEncoder ${A3_IEM_NODES_DIR}/StereoEncoderNode.cpp)

    # parseLayout reads decoder files through IEM's ConfigurationHelper, so it
    # lives beside the IEM sources and sees IEM's environment.
    add_library(a3-iem-layout${SUFFIX} STATIC ${A3_IEM_NODES_DIR}/Layout.cpp)
    a3_iem_use_iem_environment(a3-iem-layout${SUFFIX} "${SUFFIX}" ${JUCE_TARGET})
    target_include_directories(a3-iem-layout${SUFFIX} PUBLIC ${A3_IEM_PUBLIC_INCLUDE_DIR})
    # Two warnings only IEM's headers raise; the rest stays on for our file.
    target_compile_options(a3-iem-layout${SUFFIX} PRIVATE -Wall -Wextra -Werror=switch
        -Wno-ignored-qualifiers -Wno-sign-compare)

    add_library(a3-iem${SUFFIX} STATIC ${A3_IEM_NODES_DIR}/IemNode.cpp)
    target_include_directories(a3-iem${SUFFIX} PUBLIC ${A3_IEM_PUBLIC_INCLUDE_DIR})
    target_include_directories(a3-iem${SUFFIX} PRIVATE ${A3_IEM_NODES_DIR})
    target_link_libraries(a3-iem${SUFFIX} PUBLIC ${JUCE_TARGET} a3-iem-layout${SUFFIX} PRIVATE ${MultiEncoder_TARGET} ${SimpleDecoder_TARGET} ${BinauralDecoder_TARGET} ${DualDelay_TARGET}
        ${StereoEncoder_TARGET})
    target_compile_options(a3-iem${SUFFIX} PRIVATE -Wall -Wextra -Werror=switch)
endfunction()
