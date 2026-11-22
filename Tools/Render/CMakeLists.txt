# aurel_render: offline renderer used by the Python measurement harness and,
# from M2 on, the benchmark runner. In M0 it only renders reference test
# signals to validate the measurement pipeline end to end.

add_executable(aurel_render main.cpp)
target_link_libraries(aurel_render PRIVATE aurel::engine aurel::tooling)
aurel_configure_target(aurel_render)
set_target_properties(aurel_render PROPERTIES FOLDER "Tools")
