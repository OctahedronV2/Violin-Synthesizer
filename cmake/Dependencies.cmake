# Third-party dependencies, fetched at configure time and pinned to exact tags.
#
# To use a local JUCE checkout instead of downloading one, configure with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE

include(FetchContent)

set(VIOLINSYNTH_JUCE_TAG "9.0.2" CACHE STRING "JUCE git tag to build against")
set(VIOLINSYNTH_CATCH2_TAG "v3.16.0" CACHE STRING "Catch2 git tag to build tests against")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${VIOLINSYNTH_JUCE_TAG}
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    SYSTEM)
FetchContent_MakeAvailable(JUCE)

if(VIOLINSYNTH_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG ${VIOLINSYNTH_CATCH2_TAG}
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
