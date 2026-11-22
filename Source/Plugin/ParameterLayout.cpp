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

    /** Exponential (log-frequency) knob mapping. */
    juce::NormalisableRange<float> logRange (float min, float max)
    {
        return { min, max,
                 [] (float start, float end, float t) { return start * std::pow (end / start, t); },
                 [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
                 [] (float start, float end, float v) { return juce::jlimit (start, end, v); } };
    }

    juce::String hzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    std::unique_ptr<Float> frequency (const juce::String& id, const juce::String& name, float min, float max,
                                      float def, bool offAtMin = false)
    {
        return std::make_unique<Float> (
            pid (id), name, logRange (min, max), def,
            juce::AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withStringFromValueFunction ([offAtMin, min] (float v, int) {
                    return offAtMin && v <= min * 1.0001f ? juce::String ("Off") : hzText (v);
                })
                .withValueFromStringFunction ([offAtMin, min] (const juce::String& s) {
                    if (offAtMin && s.trim().equalsIgnoreCase ("off"))
                        return min;
                    const float v = s.getFloatValue();
                    return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
                }));
    }

    std::unique_ptr<Group> filterGroup()
    {
        auto g = std::make_unique<Group> ("filter", "Filter", " | ");
        // Order matches aurel::engine::FilterType. Append only.
        g->addChild (std::make_unique<Choice> (pid (ids::filterType), "Filter Type",
                                               juce::StringArray { "Ladder 24", "Ladder 12", "SVF", "Off" }, 0));
        g->addChild (frequency (ids::filterCutoff, "Filter Cutoff", 20.0f, 20000.0f, 20000.0f));
        g->addChild (percent (ids::filterResonance, "Filter Resonance", 0.0f));
        g->addChild (std::make_unique<Float> (pid (ids::filterDrive), "Filter Drive",
                                              juce::NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 0.0f,
                                              juce::AudioParameterFloatAttributes().withLabel ("dB")));
        g->addChild (percent (ids::filterBassComp, "Filter Bass Compensation", 50.0f));
        g->addChild (percent (ids::filterKeyTrack, "Filter Key Tracking", 0.0f));
        g->addChild (std::make_unique<Float> (
            pid (ids::filterSvfMode), "SVF Mode", juce::NormalisableRange<float> (0.0f, 3.0f, 0.001f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                static const char* names[] { "LP", "BP", "HP", "Notch" };
                const int i = juce::jlimit (0, 3, static_cast<int> (v));
                const float frac = v - static_cast<float> (i);
                if (i == 3 || frac < 0.005f)
                    return juce::String (names[i]);
                if (frac > 0.995f)
                    return juce::String (names[i + 1]);
                return juce::String (names[i]) + " > " + names[i + 1] + " " + juce::String (juce::roundToInt (frac * 100.0f)) + "%";
            })));
        g->addChild (frequency (ids::hpfCutoff, "High-Pass Cutoff", 20.0f, 8000.0f, 20.0f, true));
        g->addChild (percent (ids::hpfResonance, "High-Pass Resonance", 0.0f));
        return g;
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

    layout.add (filterGroup());

    layout.add (std::make_unique<Float> (pid (ids::masterVolume), "Master Volume",
                                         juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f), -6.0f,
                                         juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

} // namespace aurel::plugin
