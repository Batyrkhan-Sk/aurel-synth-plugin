#include "PluginProcessor.h"

#include "Engine/MidiParser.h"
#include "ParameterLayout.h"

namespace aurel::plugin
{

AurelAudioProcessor::AurelAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "AUREL", createParameterLayout())
{
    auto bind = [this] (const juce::String& id) {
        auto* p = parameters.getRawParameterValue (id);
        jassert (p != nullptr); // every ID used here must exist in the layout
        return p;
    };

    masterVolume = bind (ids::masterVolume);
    for (int i = 0; i < 3; ++i)
    {
        auto& o = oscillators[static_cast<std::size_t> (i)];
        o.wave = bind (ids::osc (i, ids::oscWave));
        o.octave = bind (ids::osc (i, ids::oscOctave));
        o.semi = bind (ids::osc (i, ids::oscSemi));
        o.fine = bind (ids::osc (i, ids::oscFine));
        o.pulseWidth = bind (ids::osc (i, ids::oscPulseWidth));
        o.level = bind (ids::osc (i, ids::oscLevel));
    }
    osc2Sync = bind (ids::osc2Sync);
    osc2Fm = bind (ids::osc2Fm);
    subLevel = bind (ids::subLevel);
    subOctave = bind (ids::subOctave);
    subWave = bind (ids::subWave);
    noiseLevel = bind (ids::noiseLevel);
    noiseColor = bind (ids::noiseColor);

    events.reserve (kEventCapacity);
}

engine::EngineParameters AurelAudioProcessor::readParameters() const noexcept
{
    auto get = [] (const std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
    auto index = [&] (const std::atomic<float>* p) { return static_cast<int> (std::lround (get (p))); };

    engine::EngineParameters e;
    e.masterGainDb = get (masterVolume);

    auto& v = e.voice;
    for (std::size_t i = 0; i < oscillators.size(); ++i)
    {
        const auto& b = oscillators[i];
        auto& o = v.osc[i];
        o.waveform = static_cast<dsp::Waveform> (juce::jlimit (0, 3, index (b.wave)));
        o.octave = index (b.octave);
        o.semitone = index (b.semi);
        o.fineCents = get (b.fine);
        o.pulseWidth = get (b.pulseWidth) / 100.0f;
        o.level = get (b.level) / 100.0f;
    }

    v.osc2Sync = get (osc2Sync) >= 0.5f;
    // Quadratic taper: fine control of subtle FM at the low end of the knob.
    const float fm = get (osc2Fm) / 100.0f;
    v.osc2FmDepth = fm * fm * engine::VoiceParameters::kMaxFmDepth;

    v.subLevel = get (subLevel) / 100.0f;
    v.subShape = static_cast<dsp::SubShape> (juce::jlimit (0, 1, index (subWave)));
    v.subOctave = 1 + juce::jlimit (0, 1, index (subOctave));

    v.noiseLevel = get (noiseLevel) / 100.0f;
    v.noiseColor = static_cast<dsp::NoiseColor> (juce::jlimit (0, 1, index (noiseColor)));
    return e;
}

void AurelAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    engine.prepare (sampleRate, maximumExpectedSamplesPerBlock);
}

void AurelAudioProcessor::releaseResources()
{
    engine.reset();
}

bool AurelAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.inputBuses.size() > 0 && ! layouts.getMainInputChannelSet().isDisabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void AurelAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numChannels == 0 || ! engine.isPrepared())
    {
        buffer.clear();
        return;
    }

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    const engine::EngineParameters params = readParameters();

    // Convert MIDI into engine events; event offsets are relative to the
    // segment being rendered (`segmentStart`), which only advances if the
    // event capacity is exceeded.
    events.clear();
    int segmentStart = 0;

    for (const auto metadata : midi)
    {
        const int position = juce::jlimit (0, juce::jmax (0, numSamples - 1), metadata.samplePosition);

        engine::Event e;
        if (! engine::parseMidi (metadata.data, metadata.numBytes, position - segmentStart, e))
            continue;

        if (events.size() == kEventCapacity)
        {
            const int segmentEnd = juce::jmax (segmentStart, position);
            engine.process (events, left + segmentStart,
                            right != nullptr ? right + segmentStart : nullptr,
                            segmentEnd - segmentStart, params);
            events.clear();
            segmentStart = segmentEnd;
            e.sampleOffset = position - segmentStart;
        }

        events.push_back (e);
    }

    engine.process (events, left + segmentStart,
                    right != nullptr ? right + segmentStart : nullptr,
                    numSamples - segmentStart, params);
}

juce::AudioProcessorEditor* AurelAudioProcessor::createEditor()
{
    // Temporary until the AUREL UI (M7).
    return new juce::GenericAudioProcessorEditor (*this);
}

void AurelAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("stateVersion", kStateVersion, nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AurelAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return; // corrupt or foreign state: keep current settings

    // Parameters missing from older states keep their defaults; unknown
    // ones from newer states are ignored by APVTS.
    parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace aurel::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new aurel::plugin::AurelAudioProcessor();
}
