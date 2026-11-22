#pragma once

// Plain per-block voice parameters, shared by all voices. Built by the plugin
// layer from host parameters (no JUCE types here).

#include "DSP/Oscillators/BandlimitedOscillator.h"
#include "DSP/Oscillators/Noise.h"
#include "DSP/Oscillators/SubOscillator.h"

#include <array>

namespace aurel::engine
{

struct OscillatorParameters
{
    dsp::Waveform waveform = dsp::Waveform::Saw;
    int octave = 0;           // -3 .. +3
    int semitone = 0;         // -12 .. +12
    float fineCents = 0.0f;   // -100 .. +100
    float pulseWidth = 0.5f;  // 0.01 .. 0.99
    float level = 0.0f;       // linear mixer gain, 0 .. 1
};

struct VoiceParameters
{
    static constexpr float kMaxFmDepth = 8.0f;

    std::array<OscillatorParameters, 3> osc { { { .level = 1.0f }, {}, {} } };

    bool osc2Sync = false;     // OSC2 hard-synced to OSC1
    float osc2FmDepth = 0.0f;  // linear through-zero FM index, OSC1 -> OSC2, 0 .. kMaxFmDepth

    dsp::SubShape subShape = dsp::SubShape::Square;
    int subOctave = 1;         // 1 or 2 octaves below OSC1
    float subLevel = 0.0f;

    dsp::NoiseColor noiseColor = dsp::NoiseColor::White;
    float noiseLevel = 0.0f;
};

} // namespace aurel::engine
