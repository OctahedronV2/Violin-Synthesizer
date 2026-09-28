# Optional AddressSanitizer + UndefinedBehaviorSanitizer instrumentation.
# Enable with -DVIOLINSYNTH_ENABLE_SANITIZERS=ON (GCC/Clang only).

function(violinsynth_enable_sanitizers target)
    if(NOT VIOLINSYNTH_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        message(WARNING "Sanitizers are not configured for MSVC; ignoring VIOLINSYNTH_ENABLE_SANITIZERS")
        return()
    endif()

    set(flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_compile_options(${target} PUBLIC ${flags})
    target_link_options(${target} PUBLIC ${flags})
endfunction()

# Optional RealtimeSanitizer instrumentation (Clang 20 or later). Functions
# marked VIOLINSYNTH_NONBLOCKING (source/engine/Realtime.h) abort on any
# allocation, lock or system call. Enable with -DVIOLINSYNTH_ENABLE_RTSAN=ON.
function(violinsynth_enable_rtsan target)
    if(NOT VIOLINSYNTH_ENABLE_RTSAN)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20)
        message(FATAL_ERROR "VIOLINSYNTH_ENABLE_RTSAN needs Clang 20 or later")
    endif()

    # -Wfunction-effects would flag every call from a nonblocking function
    # into code that isn't annotated (JUCE, the standard library); the
    # sanitizer checks what those calls actually do at run time instead.
    target_compile_options(${target} PUBLIC -fsanitize=realtime -fno-omit-frame-pointer -Wno-function-effects -Wno-perf-constraint-implies-noexcept)
    target_link_options(${target} PUBLIC -fsanitize=realtime)
endfunction()
