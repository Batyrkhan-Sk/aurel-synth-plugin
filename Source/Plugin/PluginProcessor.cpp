#include "PluginProcessor.h"

#include "Engine/MidiParser.h"
#include "ParameterLayout.h"

namespace aurel::plugin
{

AurelAudioProcessor::AurelAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "AUREL", createParameterLayout())
{
    masterVolume = parameters.getRawParameterValue (ids::masterVolume);
    jassert (masterVolume != nullptr);
    events.reserve (kEventCapacity);
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

    engine::EngineParameters params;
    params.masterGainDb = masterVolume->load (std::memory_order_relaxed);

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
