# Project-wide warning flags. JUCE compiles its module sources into the
# targets that link it, so the flags are attached to our own source files
# rather than to whole targets; otherwise they would also apply to JUCE.

# Usage: violinsynth_set_warnings(<target> <source>...)
function(violinsynth_set_warnings target)
    if(MSVC)
        set(flags /W4 /permissive-)
    else()
        set(flags
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wcast-align
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2)
    endif()

    set_property(SOURCE ${ARGN} TARGET_DIRECTORY ${target} APPEND PROPERTY COMPILE_OPTIONS ${flags})
endfunction()
