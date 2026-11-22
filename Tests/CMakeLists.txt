include(FetchContent)

# Catch2 v3, pinned. Fetched at configure time (requires network once; the
# source is cached in the build directory afterwards).
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)

add_executable(aurel_tests
    Support/AllocationCounter.cpp
    Unit/DSP/ConversionsTests.cpp
    Unit/DSP/DenormalsTests.cpp
    Unit/Tooling/WavFileTests.cpp
    RealtimeSafety/AllocationCounterTests.cpp)

target_include_directories(aurel_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aurel_tests PRIVATE
    aurel::dsp
    aurel::tooling
    Catch2::Catch2WithMain)
aurel_configure_target(aurel_tests)
set_target_properties(aurel_tests PROPERTIES FOLDER "Tests")

catch_discover_tests(aurel_tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
