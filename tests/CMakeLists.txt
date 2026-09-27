set(VIOLINSYNTH_TEST_SOURCES
    PluginProcessorTests.cpp)

add_executable(ViolinSynthTests ${VIOLINSYNTH_TEST_SOURCES})

# Link against the plugin's shared-code library so the tests exercise exactly
# the code the plugin formats ship, and reuse its JUCE build instead of
# compiling the modules a second time.
target_link_libraries(ViolinSynthTests PRIVATE ViolinSynth Catch2::Catch2WithMain)
target_include_directories(ViolinSynthTests SYSTEM PRIVATE
    $<TARGET_PROPERTY:ViolinSynth,INCLUDE_DIRECTORIES>)
target_compile_definitions(ViolinSynthTests PRIVATE
    $<TARGET_PROPERTY:ViolinSynth,COMPILE_DEFINITIONS>)

violinsynth_set_warnings(ViolinSynthTests ${VIOLINSYNTH_TEST_SOURCES})

include(Catch)
catch_discover_tests(ViolinSynthTests)
