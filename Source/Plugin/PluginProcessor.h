#pragma once

#include "Engine/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace aurel::plugin
{

class AurelAudioProcessor final : public juce::AudioProcessor
{
public:
    AurelAudioProcessor();

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

private:
    /** Upper bound on events per engine call. If a block carries more, the
        block is rendered in segments, so no event is ever dropped. */
    static constexpr std::size_t kEventCapacity = 2048;

    /** Reads the cached atomics into the engine's plain parameter snapshot. */
    [[nodiscard]] engine::EngineParameters readParameters() const noexcept;

    struct OscillatorBinding
    {
        std::atomic<float>* wave = nullptr;
        std::atomic<float>* octave = nullptr;
        std::atomic<float>* semi = nullptr;
        std::atomic<float>* fine = nullptr;
        std::atomic<float>* pulseWidth = nullptr;
        std::atomic<float>* level = nullptr;
    };

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* masterVolume = nullptr;
    std::array<OscillatorBinding, 3> oscillators {};
    std::atomic<float>* osc2Sync = nullptr;
    std::atomic<float>* osc2Fm = nullptr;
    std::atomic<float>* subLevel = nullptr;
    std::atomic<float>* subOctave = nullptr;
    std::atomic<float>* subWave = nullptr;
    std::atomic<float>* noiseLevel = nullptr;
    std::atomic<float>* noiseColor = nullptr;

    engine::Engine engine;
    std::vector<engine::Event> events; // capacity reserved in the constructor

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AurelAudioProcessor)
};

} // namespace aurel::plugin
