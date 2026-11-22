# aurel_engine: voices, MIDI events and the top-level engine. JUCE-free.

add_library(aurel_engine STATIC
    Engine.cpp
    MidiParser.cpp
    Voice.cpp
    VoiceManager.cpp)
add_library(aurel::engine ALIAS aurel_engine)

target_include_directories(aurel_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(aurel_engine PUBLIC aurel::dsp)
aurel_configure_target(aurel_engine)
set_target_properties(aurel_engine PROPERTIES FOLDER "Core")
