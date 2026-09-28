set(VIOLINSYNTH_SOURCES
    dsp/BowedString.cpp
    dsp/PartitionedConvolution.cpp
    engine/Body.cpp
    engine/GuitarAmp.cpp
    engine/StringAllocator.cpp
    engine/OutputChain.cpp
    engine/ViolinEngine.cpp
    engine/StringVoice.cpp
    engine/SympatheticStrings.cpp
    engine/Violin.cpp
    plugin/EditorComponents.cpp
    plugin/FactoryPresets.cpp
    plugin/HostKeyboardFocus.cpp
    plugin/LookAndFeel.cpp
    plugin/Parameters.cpp
    plugin/PluginEditor.cpp
    plugin/PluginProcessor.cpp
    plugin/PresetManager.cpp)

target_sources(Octavio PRIVATE ${VIOLINSYNTH_SOURCES})
target_include_directories(Octavio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

violinsynth_set_warnings(Octavio ${VIOLINSYNTH_SOURCES})

# Measured body impulse responses (research/scripts/export_plugin_assets.py).
juce_add_binary_data(ViolinSynthBodies
    NAMESPACE BinaryData
    HEADER_NAME BinaryData.h
    SOURCES
        "${PROJECT_SOURCE_DIR}/resources/bodies/levaggi.wav"
        "${PROJECT_SOURCE_DIR}/resources/bodies/klimke.wav"
        "${PROJECT_SOURCE_DIR}/resources/bodies/stoppani.wav"
        "${PROJECT_SOURCE_DIR}/resources/bodies/iowa.wav"
        "${PROJECT_SOURCE_DIR}/resources/bodies/acoustic-guitar.wav")
set_target_properties(ViolinSynthBodies PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
target_link_libraries(Octavio PRIVATE ViolinSynthBodies)

# PFFFT (BSD-like licence, third_party/pffft/README.md): the FFT behind the
# measured-body convolution, with SSE and NEON on every platform.
add_library(ViolinSynthPffft STATIC "${PROJECT_SOURCE_DIR}/third_party/pffft/pffft.c")
target_include_directories(ViolinSynthPffft SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/pffft")
set_target_properties(ViolinSynthPffft PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
if(NOT WIN32)
    target_link_libraries(ViolinSynthPffft PRIVATE m)
endif()
target_link_libraries(Octavio PRIVATE ViolinSynthPffft)
