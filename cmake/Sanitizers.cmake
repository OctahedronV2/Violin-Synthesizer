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
