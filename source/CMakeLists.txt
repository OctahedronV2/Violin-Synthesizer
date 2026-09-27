set(VIOLINSYNTH_SOURCES
    plugin/Parameters.cpp
    plugin/PlaceholderVoice.cpp
    plugin/PluginEditor.cpp
    plugin/PluginProcessor.cpp)

target_sources(ViolinSynth PRIVATE ${VIOLINSYNTH_SOURCES})
target_include_directories(ViolinSynth PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

violinsynth_set_warnings(ViolinSynth ${VIOLINSYNTH_SOURCES})
