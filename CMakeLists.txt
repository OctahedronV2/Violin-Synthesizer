cmake_minimum_required(VERSION 3.24)

# These must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# Link the MSVC runtime statically so the plugin loads on machines that do not
# have the matching Visual C++ Redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Octavio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep all JUCE-generated targets tidy in IDEs.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(VIOLINSYNTH_BUILD_TESTS "Build the unit test executable" ON)
option(VIOLINSYNTH_COPY_PLUGIN_AFTER_BUILD "Install plugins into the user's plugin folders after each build" OFF)
option(VIOLINSYNTH_ENABLE_SANITIZERS "Build with AddressSanitizer and UBSan (GCC/Clang only)" OFF)
option(VIOLINSYNTH_ENABLE_RTSAN "Build with RealtimeSanitizer (Clang 20 or later)" OFF)
# Link-time optimisation in Release builds. It makes each plugin, test and
# benchmark link redo code generation for all of JUCE, which is most of a CI
# build, so pull request builds turn it off (docs/CI.md).
option(VIOLINSYNTH_ENABLE_LTO "Link-time optimisation in Release builds" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(CompilerWarnings)
include(Sanitizers)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
set(VIOLINSYNTH_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VIOLINSYNTH_FORMATS AU)
endif()

juce_add_plugin(Octavio
    PRODUCT_NAME "Octavio"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "OctahedronV2"
    BUNDLE_ID "com.octahedronv2.octavio"
    # Four-character IDs that hosts use to identify the plugin. Changing them
    # after release breaks saved projects, so treat them as permanent.
    PLUGIN_MANUFACTURER_CODE Octa
    PLUGIN_CODE Vsyn
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

add_subdirectory(source)

target_compile_definitions(Octavio
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Octavio
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)

if(VIOLINSYNTH_ENABLE_LTO)
    target_link_libraries(Octavio PUBLIC juce::juce_recommended_lto_flags)
endif()

violinsynth_enable_sanitizers(Octavio)
violinsynth_enable_rtsan(Octavio)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(VIOLINSYNTH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
