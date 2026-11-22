#pragma once

#include "DSP/Oscillators/BandlimitedOscillator.h"

#include <span>
#include <string_view>

namespace aurel::render
{

/** Kernel names: naive, polyblep2, sinc4, sinc6, sinc8, sinc12. */
[[nodiscard]] bool isKnownKernel (std::string_view name);
[[nodiscard]] bool parseWaveform (std::string_view name, dsp::Waveform& out);

/** Renders a raw oscillator (no engine, no envelope) into `out`. The output
    is shifted to compensate the kernel latency. */
[[nodiscard]] bool renderOscillator (std::string_view kernel, dsp::Waveform waveform, double frequency,
                                     double pulseWidth, double sampleRate, std::span<float> out);

/** Times every kernel x waveform and prints a table. Returns process exit code. */
int benchmarkOscillators();

} // namespace aurel::render
