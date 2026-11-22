# AUREL plugin (VST3 + Standalone).
#
# PLUGIN_MANUFACTURER_CODE and PLUGIN_CODE form the plugin's identity in
# hosts (the VST3 class ID is derived from them). They must NEVER change
# once projects or presets exist that reference the plugin.

juce_add_plugin(AUREL
    COMPANY_NAME                "AUREL"
    PLUGIN_MANUFACTURER_CODE    Aurl
    PLUGIN_CODE                 Aur1
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "AUREL"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

set(AUREL_PLUGIN_SOURCES
    ParameterLayout.cpp
    PluginProcessor.cpp)
target_sources(AUREL PRIVATE ${AUREL_PLUGIN_SOURCES})

# This target also compiles JUCE's module sources, so AUREL's strict
# warning settings are applied per source file, to our files only.
# juce_recommended_warning_flags covers the rest.
if(MSVC AND AUREL_WARNINGS_AS_ERRORS)
    set_source_files_properties(${AUREL_PLUGIN_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4;/WX")
endif()

target_compile_definitions(AUREL
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AUREL
    PRIVATE
        aurel::engine
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

set_target_properties(AUREL PROPERTIES FOLDER "Plugin")
