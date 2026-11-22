# Compiler settings applied to AUREL's own targets only (never to JUCE or
# third-party code).
#
#   aurel_configure_target(<target>)
#
# Floating point: keep the compiler default (/fp:precise). /fp:fast lets MSVC
# assume no NaN/Inf, which silently removes the NaN guards used by the tests.

#
# Warning flags are always PRIVATE. For INTERFACE (header-only) libraries only
# the language standard propagates: the plugin target compiles JUCE's own
# module sources, so propagating /W4 /WX to it would fail on JUCE warnings.
# Header-only code is still compiled at /W4 /WX through the test targets.

function(aurel_configure_target target)
    get_target_property(_type ${target} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_features(${target} INTERFACE cxx_std_20)
        return()
    endif()
    set(_scope PRIVATE)

    target_compile_features(${target} ${_scope} cxx_std_20)

    if(MSVC)
        target_compile_options(${target} ${_scope}
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
            /MP)
        if(AUREL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} ${_scope} /WX)
        endif()
        target_compile_definitions(${target} ${_scope}
            _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(${target} ${_scope}
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
        if(AUREL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} ${_scope} -Werror)
        endif()
    endif()
endfunction()
