#include "ParameterLayout.h"

namespace aurel::plugin
{

juce::String ids::osc (int index, const char* suffix)
{
    return "osc" + juce::String (index + 1) + "_" + suffix;
}

namespace
{
    using Float = juce::AudioParameterFloat;
    using Int = juce::AudioParameterInt;
    using Choice = juce::AudioParameterChoice;
    using Bool = juce::AudioParameterBool;
    using Group = juce::AudioProcessorParameterGroup;

    constexpr int kVersion = 1; // all parameters introduced in the first (unreleased) version

    juce::ParameterID pid (const juce::String& id) { return { id, kVersion }; }

    std::unique_ptr<Float> percent (const juce::String& id, const juce::String& name, float def,
                                    float min = 0.0f, float max = 100.0f)
    {
        return std::make_unique<Float> (pid (id), name, juce::NormalisableRange<float> (min, max, 0.1f), def,
                                        juce::AudioParameterFloatAttributes().withLabel ("%"));
    }

    std::unique_ptr<Group> oscillatorGroup (int index)
    {
        const auto n = juce::String (index + 1);
        auto g = std::make_unique<Group> ("osc" + n, "Oscillator " + n, " | ");

        // Order matches aurel::dsp::Waveform. Append only.
        g->addChild (std::make_unique<Choice> (pid (ids::osc (index, ids::oscWave)), "Osc " + n + " Wave",
                                               juce::StringArray { "Sine", "Triangle", "Saw", "Pulse" }, 2));
        g->addChild (std::make_unique<Int> (pid (ids::osc (index, ids::oscOctave)), "Osc " + n + " Octave", -3, 3, 0));
        g->addChild (std::make_unique<Int> (pid (ids::osc (index, ids::oscSemi)), "Osc " + n + " Semitone", -12, 12, 0));
        g->addChild (std::make_unique<Float> (pid (ids::osc (index, ids::oscFine)), "Osc " + n + " Fine",
                                              juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                              juce::AudioParameterFloatAttributes().withLabel ("ct")));
        g->addChild (percent (ids::osc (index, ids::oscPulseWidth), "Osc " + n + " Pulse Width", 50.0f, 1.0f, 99.0f));
        g->addChild (percent (ids::osc (index, ids::oscLevel), "Osc " + n + " Level", index == 0 ? 100.0f : 0.0f));

        if (index == 1)
        {
            g->addChild (std::make_unique<Bool> (pid (ids::osc2Sync), "Osc 2 Sync", false));
            g->addChild (percent (ids::osc2Fm, "Osc 2 FM (from Osc 1)", 0.0f));
        }
        return g;
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < 3; ++i)
        layout.add (oscillatorGroup (i));

    auto sub = std::make_unique<Group> ("sub", "Sub Oscillator", " | ");
    sub->addChild (percent (ids::subLevel, "Sub Level", 0.0f));
    // Order matches aurel::dsp::SubShape. Append only.
    sub->addChild (std::make_unique<Choice> (pid (ids::subWave), "Sub Wave", juce::StringArray { "Square", "Sine" }, 0));
    sub->addChild (std::make_unique<Choice> (pid (ids::subOctave), "Sub Octave", juce::StringArray { "-1 Oct", "-2 Oct" }, 0));
    layout.add (std::move (sub));

    auto noise = std::make_unique<Group> ("noise", "Noise", " | ");
    noise->addChild (percent (ids::noiseLevel, "Noise Level", 0.0f));
    // Order matches aurel::dsp::NoiseColor. Append only.
    noise->addChild (std::make_unique<Choice> (pid (ids::noiseColor), "Noise Color", juce::StringArray { "White", "Pink" }, 0));
    layout.add (std::move (noise));

    layout.add (std::make_unique<Float> (pid (ids::masterVolume), "Master Volume",
                                         juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f), -6.0f,
                                         juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

} // namespace aurel::plugin
