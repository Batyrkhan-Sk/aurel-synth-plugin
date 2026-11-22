# aurel_tooling: helpers shared by the test suite and offline tools
# (WAV I/O, etc.). Never linked into the plugin.

add_library(aurel_tooling INTERFACE)
add_library(aurel::tooling ALIAS aurel_tooling)

target_include_directories(aurel_tooling INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
aurel_configure_target(aurel_tooling)
