# Octavio 2 (docs/OCTAVIO2_PLAN.md): the new engine (core/), its offline renderer (render/) and
# the plugin (plugin/). It installs side by side with Octavio 1 (Jake, 2026-10-01), so it has its
# own name, plugin code and bundle ID.

# The offline renderer: the same engine code as the plugin, without JUCE.
add_executable(Octavio2Render render/render.cpp "${PROJECT_SOURCE_DIR}/source/dsp/PartitionedConvolution.cpp")
target_include_directories(Octavio2Render PRIVATE "${PROJECT_SOURCE_DIR}/source")
target_link_libraries(Octavio2Render PRIVATE ViolinSynthPffft)

if(VIOLINSYNTH_RENDER_ONLY)
    return()
endif()

juce_add_plugin(Octavio2
    PRODUCT_NAME "Octavio 2"
    VERSION 2.1.0
    COMPANY_NAME "OctahedronV2"
    BUNDLE_ID "com.octahedronv2.octavio2"
    # Permanent once released: hosts save projects against these.
    PLUGIN_MANUFACTURER_CODE Octa
    PLUGIN_CODE Oct2
    FORMATS ${VIOLINSYNTH_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE
    COPY_PLUGIN_AFTER_BUILD ${VIOLINSYNTH_COPY_PLUGIN_AFTER_BUILD})

# Bodies (body-radiation research) and halls (data/halls/SOURCES.md, CC BY).
juce_add_binary_data(Octavio2Data
    NAMESPACE Octavio2BinaryData
    HEADER_NAME Octavio2BinaryData.h
    SOURCES
        data/bodies/stoppani-48k.wav
        data/bodies/klimke-48k.wav
        data/bodies/levaggi-48k.wav
        data/bodies/iowa-48k.wav
        data/mics/front-48k.wav
        data/mics/above-48k.wav
        data/mics/ear-48k.wav
        data/mics/side-48k.wav
        data/halls/arvedi-near.wav
        data/halls/arvedi-far.wav
        data/halls/detmold.wav
        data/halls/church.wav
        data/halls/maida-vale-4.wav
        data/halls/maida-vale-5.wav
        data/halls/wdr-control-room.wav
        data/halls/wdr-studio.wav
        # the interface's faces (plugin/fonts/OFL.txt)
        plugin/fonts/InstrumentSerif-Regular.ttf
        plugin/fonts/IBMPlexSans-Regular.ttf
        plugin/fonts/IBMPlexSans-SemiBold.ttf
        plugin/fonts/IBMPlexMono-Regular.ttf)
set_target_properties(Octavio2Data PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(Octavio2 PRIVATE
    plugin/Parameters.cpp
    plugin/PluginEditor.cpp
    plugin/PluginProcessor.cpp
    plugin/ui/Controls.cpp
    plugin/ui/CurvesView.cpp
    plugin/ui/Keyboard.cpp
    plugin/ui/MidiView.cpp
    plugin/ui/NoteTrack.cpp
    plugin/ui/ArticulationView.cpp
    plugin/ui/PlaceholderView.cpp
    plugin/ui/PlayView.cpp
    plugin/ui/Theme.cpp
    plugin/ui/ToneView.cpp
    "${PROJECT_SOURCE_DIR}/source/dsp/PartitionedConvolution.cpp"
    "${PROJECT_SOURCE_DIR}/source/plugin/HostKeyboardFocus.cpp"
    # M7: MTS-ESP client (0BSD, loads the MTS-ESP library at run time if installed)
    "${PROJECT_SOURCE_DIR}/third_party/mts-esp/libMTSClient.cpp")
target_include_directories(Octavio2 PUBLIC "${PROJECT_SOURCE_DIR}/source" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(Octavio2
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(Octavio2
    PRIVATE
        Octavio2Data
        ViolinSynthPffft
        juce::juce_audio_utils
        juce::juce_dsp
        ${CMAKE_DL_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags)
if(VIOLINSYNTH_ENABLE_LTO)
    target_link_libraries(Octavio2 PUBLIC juce::juce_recommended_lto_flags)
endif()
violinsynth_enable_sanitizers(Octavio2)

if(VIOLINSYNTH_BUILD_TESTS)
    add_executable(Octavio2Tests tests/Octavio2Tests.cpp)
    target_link_libraries(Octavio2Tests PRIVATE Octavio2 Catch2::Catch2WithMain)
    target_include_directories(Octavio2Tests SYSTEM PRIVATE $<TARGET_PROPERTY:Octavio2,INCLUDE_DIRECTORIES>)
    target_compile_definitions(Octavio2Tests PRIVATE $<TARGET_PROPERTY:Octavio2,COMPILE_DEFINITIONS>)
    include(Catch)
    catch_discover_tests(Octavio2Tests)
endif()
