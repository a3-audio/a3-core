add_library(audio_dependencies OBJECT)

target_link_libraries(audio_dependencies
    PUBLIC
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(audio_dependencies
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        DONT_SET_USING_JUCE_NAMESPACE=1
    INTERFACE
    $<TARGET_PROPERTY:audio_dependencies,COMPILE_DEFINITIONS>
)

target_include_directories(audio_dependencies
    INTERFACE
        $<TARGET_PROPERTY:audio_dependencies,INCLUDE_DIRECTORIES>)

add_library("a3-audio-engine" STATIC)

set_target_properties("a3-audio-engine" PROPERTIES
    POSITION_INDEPENDENT_CODE TRUE)

_juce_initialise_target("a3-audio-engine")
juce_generate_juce_header("a3-audio-engine")
a3_precompile_juce_header("a3-audio-engine")

target_sources("a3-audio-engine" PRIVATE
    ChunkedRender.cc
    ChunkedRender.hh
    ControlSurface.cc
    ControlSurface.hh
    OutputOrder.cc
    OutputOrder.hh
    SpatBackendInternal.cc
    SpatBackendInternal.hh
    SpeakerTest.cc
    SpeakerTest.hh
)

target_include_directories("a3-audio-engine" PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries("a3-audio-engine" PUBLIC
    a3-motion-engine
    audio_dependencies
)
