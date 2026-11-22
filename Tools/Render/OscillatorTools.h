#pragma once

#include "DSP/Oscillators/BandlimitedOscillator.h"

#include <span>
#include <string>
#include <string_view>

namespace aurel::render
{

struct OscRenderOptions
{
    std::string kernel = "sinc6";
    dsp::Waveform waveform = dsp::Waveform::Saw;
    double frequency = 440.0;  // fundamental (= master frequency when synced)
    double pulseWidth = 0.5;
    double sampleRate = 96000.0;
    double syncRatio = 0.0;    // > 0: output a slave at frequency * ratio, hard-synced to a master at frequency
    double fmDepth = 0.0;      // > 0: linear through-zero FM by a sine at frequency * fmRatio
    double fmRatio = 1.0;
};

/** Kernel names: naive, polyblep2, sinc4, sinc6, sinc8, sinc12. */
[[nodiscard]] bool isKnownKernel (std::string_view name);
[[nodiscard]] bool parseWaveform (std::string_view name, dsp::Waveform& out);

/** Renders a raw oscillator (no engine, no envelope) into `out`, latency
    compensated. */
[[nodiscard]] bool renderOscillator (const OscRenderOptions& options, std::span<float> out);

/** Times every kernel x waveform and prints a table. Returns process exit code. */
int benchmarkOscillators();

} // namespace aurel::render
