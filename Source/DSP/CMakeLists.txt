# aurel_dsp: pure C++20 DSP building blocks. No JUCE, no allocation in
# processing paths. Header-only until the first module needs a .cpp, at which
# point this becomes a STATIC library.

add_library(aurel_dsp INTERFACE)
add_library(aurel::dsp ALIAS aurel_dsp)

target_include_directories(aurel_dsp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
aurel_configure_target(aurel_dsp)
