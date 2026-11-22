#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace aurel::plugin
{

/** Host parameter IDs. These strings are stored in DAW projects and presets:
    once released, an ID must never be renamed or reused for something else.
    New parameters get version hint = the plugin version that introduced them. */
namespace ids
{
    inline constexpr const char* masterVolume = "master_volume";
} // namespace ids

/** Version stored in the plugin state, for future migrations. */
inline constexpr int kStateVersion = 1;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace aurel::plugin
