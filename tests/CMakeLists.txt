set(VIOLINSYNTH_TEST_SOURCES
    PluginProcessorTests.cpp
    EngineTests.cpp
    Phase4Tests.cpp
    Phase5Tests.cpp
    BowNoiseTests.cpp
    ControlRateTests.cpp
    BodyConvolutionTests.cpp
    PresetTests.cpp
    KeyboardInputTests.cpp
    SoundCheckTests.cpp
    StringAllocatorTests.cpp
    StringDspTests.cpp)

add_executable(ViolinSynthTests ${VIOLINSYNTH_TEST_SOURCES})

# Link against the plugin's shared-code library so the tests exercise exactly
# the code the plugin formats ship, and reuse its JUCE build instead of
# compiling the modules a second time.
target_link_libraries(ViolinSynthTests PRIVATE ViolinSynth Catch2::Catch2WithMain)
target_include_directories(ViolinSynthTests SYSTEM PRIVATE
    $<TARGET_PROPERTY:ViolinSynth,INCLUDE_DIRECTORIES>)
target_compile_definitions(ViolinSynthTests PRIVATE
    $<TARGET_PROPERTY:ViolinSynth,COMPILE_DEFINITIONS>
    VIOLINSYNTH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

violinsynth_set_warnings(ViolinSynthTests ${VIOLINSYNTH_TEST_SOURCES})

include(Catch)
catch_discover_tests(ViolinSynthTests)

# CPU benchmarks (docs/PHASE7.md): not a test, run by hand or under Callgrind in CI.
add_executable(ViolinSynthBench bench/Benchmark.cpp)
target_link_libraries(ViolinSynthBench PRIVATE ViolinSynth)
target_include_directories(ViolinSynthBench SYSTEM PRIVATE
    $<TARGET_PROPERTY:ViolinSynth,INCLUDE_DIRECTORIES>)
target_compile_definitions(ViolinSynthBench PRIVATE
    $<TARGET_PROPERTY:ViolinSynth,COMPILE_DEFINITIONS>)
violinsynth_set_warnings(ViolinSynthBench bench/Benchmark.cpp)
