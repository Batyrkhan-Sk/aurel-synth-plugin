#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace aurel::plugin
{

/** Host parameter IDs. These strings are stored in DAW projects and presets:
    once released, an ID must never be renamed or reused for something else.
    Choice parameters must never reorder existing entries (append only). */
namespace ids
{
    inline constexpr const char* masterVolume = "master_volume";

    // Per oscillator: prefix "osc1_", "osc2_", "osc3_" + suffix below.
    inline constexpr const char* oscWave = "wave";
    inline constexpr const char* oscOctave = "octave";
    inline constexpr const char* oscSemi = "semi";
    inline constexpr const char* oscFine = "fine";
    inline constexpr const char* oscPulseWidth = "pw";
    inline constexpr const char* oscLevel = "level";

    inline constexpr const char* osc2Sync = "osc2_sync";
    inline constexpr const char* osc2Fm = "osc2_fm";

    inline constexpr const char* subLevel = "sub_level";
    inline constexpr const char* subOctave = "sub_octave";
    inline constexpr const char* subWave = "sub_wave";

    inline constexpr const char* noiseLevel = "noise_level";
    inline constexpr const char* noiseColor = "noise_color";

    /** "osc2_level" etc. index is 0-based. */
    juce::String osc (int index, const char* suffix);
} // namespace ids

/** Version stored in the plugin state, for future migrations. */
inline constexpr int kStateVersion = 1;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace aurel::plugin
