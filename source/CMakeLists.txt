set(VIOLINSYNTH_SOURCES
    dsp/BowedString.cpp
    engine/Body.cpp
    engine/OutputChain.cpp
    engine/ViolinEngine.cpp
    engine/ViolinVoice.cpp
    plugin/Parameters.cpp
    plugin/PluginEditor.cpp
    plugin/PluginProcessor.cpp)

target_sources(ViolinSynth PRIVATE ${VIOLINSYNTH_SOURCES})
target_include_directories(ViolinSynth PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

violinsynth_set_warnings(ViolinSynth ${VIOLINSYNTH_SOURCES})

# Measured body impulse responses (research/scripts/export_plugin_assets.py).
juce_add_binary_data(ViolinSynthBodies
    NAMESPACE BinaryData
    HEADER_NAME BinaryData.h
    SOURCES
        "${PROJECT_SOURCE_DIR}/resources/bodies/levaggi.wav"
        "${PROJECT_SOURCE_DIR}/resources/bodies/klimke.wav"
        "${PROJECT_SOURCE_DIR}/resources/bodies/stoppani.wav"
        "${PROJECT_SOURCE_DIR}/resources/bodies/iowa.wav")
set_target_properties(ViolinSynthBodies PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
target_link_libraries(ViolinSynth PRIVATE ViolinSynthBodies)
