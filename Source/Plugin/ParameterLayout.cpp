#include "ParameterLayout.h"

namespace aurel::plugin
{

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ids::masterVolume, 1 },
        "Master Volume",
        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f),
        -6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

} // namespace aurel::plugin
