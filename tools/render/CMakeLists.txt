# Offline renderer for fast sound tweaks: MIDI file in, WAV out, through the
# engine alone. It compiles only the DSP and engine sources and the JUCE
# modules they use, with no plugin wrapper, editor or tests, so after a DSP
# edit an incremental build recompiles one file and relinks in seconds.
#
#   cmake -B build-render -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DVIOLINSYNTH_RENDER_ONLY=ON
#   cmake --build build-render --target ViolinSynthRender

set(VIOLINSYNTH_RENDER_SOURCES
    "${PROJECT_SOURCE_DIR}/source/dsp/BowedString.cpp"
    "${PROJECT_SOURCE_DIR}/source/dsp/PartitionedConvolution.cpp"
    "${PROJECT_SOURCE_DIR}/source/engine/Body.cpp"
    "${PROJECT_SOURCE_DIR}/source/engine/GuitarAmp.cpp"
    "${PROJECT_SOURCE_DIR}/source/engine/StringAllocator.cpp"
    "${PROJECT_SOURCE_DIR}/source/engine/OutputChain.cpp"
    "${PROJECT_SOURCE_DIR}/source/engine/Radiation.cpp"
    "${PROJECT_SOURCE_DIR}/source/engine/ViolinEngine.cpp"
    "${PROJECT_SOURCE_DIR}/source/engine/StringVoice.cpp"
    "${PROJECT_SOURCE_DIR}/source/engine/SympatheticStrings.cpp"
    "${PROJECT_SOURCE_DIR}/source/engine/Violin.cpp")

add_executable(ViolinSynthRender OfflineRender.cpp ${VIOLINSYNTH_RENDER_SOURCES})
target_include_directories(ViolinSynthRender PRIVATE "${PROJECT_SOURCE_DIR}/source")
target_compile_definitions(ViolinSynthRender PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(ViolinSynthRender PRIVATE
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_recommended_config_flags
    ViolinSynthBodies
    ViolinSynthPffft)
