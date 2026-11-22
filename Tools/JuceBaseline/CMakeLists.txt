# aurel_juce_baseline: renders JUCE's built-in filters under the same test
# conditions as AUREL's, so the custom filters can be compared objectively
# (ADR 0001: custom DSP must show a measurable benefit). Development tool
# only; never shipped.

juce_add_console_app(aurel_juce_baseline PRODUCT_NAME "aurel_juce_baseline")
target_sources(aurel_juce_baseline PRIVATE main.cpp)
target_compile_definitions(aurel_juce_baseline PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(aurel_juce_baseline
    PRIVATE
        aurel::dsp
        aurel::tooling
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
set_target_properties(aurel_juce_baseline PROPERTIES FOLDER "Tools")
