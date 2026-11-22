cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime for every target (ours, JUCE, Catch2) so the shipped
# plugin does not depend on the VC++ redistributable. Must be set before any
# target is created.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(AUREL
    VERSION 0.1.0
    DESCRIPTION "AUREL virtual analog synthesizer"
    LANGUAGES C CXX) # JUCE compiles some C sources (e.g. bundled codecs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(AUREL_BUILD_TESTS "Build the Catch2 test suite" ON)
option(AUREL_BUILD_TOOLS "Build offline render / benchmark tools" ON)
option(AUREL_BUILD_PLUGIN "Configure JUCE and build the plugin" ON)
option(AUREL_WARNINGS_AS_ERRORS "Treat warnings in AUREL code as errors" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AurelCompilerSettings)

# --- JUCE-free core -------------------------------------------------------
add_subdirectory(Source/DSP)
# Source/Engine (voices, MIDI events, engine) arrives in milestone M1.

# --- Tooling shared by tests and offline tools -----------------------------
add_subdirectory(Tools/Common)

if(AUREL_BUILD_TOOLS)
    add_subdirectory(Tools/Render)
endif()

# --- JUCE / plugin --------------------------------------------------------
if(AUREL_BUILD_PLUGIN)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
        message(FATAL_ERROR
            "JUCE submodule missing. Run: git submodule update --init --depth 1")
    endif()
    add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)
    # Plugin target is added in milestone M1 (Source/Plugin).
endif()

# --- Tests ----------------------------------------------------------------
if(AUREL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
