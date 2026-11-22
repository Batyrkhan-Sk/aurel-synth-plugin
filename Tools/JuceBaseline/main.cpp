// aurel_juce_baseline --out <dir>
//
// Writes (96 kHz, mono float WAV):
//   juce_ladder_ir_<fc>.wav       LPF24, resonance 0, drive 1: impulse response
//                                 to a 1e-4 impulse (small-signal), 1 s
//   juce_ladder_selfosc_<fc>.wav  LPF24, resonance 1: 4 s after a 0.01 impulse
// Prints:
//   SVF equivalence: max |JUCE TPT SVF - AUREL SVF| over 1 s of noise
//   CPU: JUCE ladder ns/sample (512-sample blocks)

#include "DSP/Filters/StateVariableFilter.h"
#include "WavFile.h"

#include <juce_dsp/juce_dsp.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace
{
constexpr double kRate = 96000.0;
constexpr int kBlock = 512;

std::vector<float> renderLadder (float cutoff, float resonance, float impulse, int numSamples)
{
    juce::dsp::LadderFilter<float> f;
    f.prepare ({ kRate, static_cast<juce::uint32> (kBlock), 1 });
    f.setMode (juce::dsp::LadderFilterMode::LPF24);
    f.setCutoffFrequencyHz (cutoff);
    f.setResonance (resonance);
    f.setDrive (1.0f);
    f.reset(); // parameters take effect without smoothing ramps

    std::vector<float> out (static_cast<std::size_t> (numSamples), 0.0f);
    out[0] = impulse;
    for (int start = 0; start < numSamples; start += kBlock)
    {
        const int n = std::min (kBlock, numSamples - start);
        float* channels[] = { out.data() + start };
        juce::dsp::AudioBlock<float> block (channels, 1, static_cast<size_t> (n));
        f.process (juce::dsp::ProcessContextReplacing<float> (block));
    }
    return out;
}

bool write (const std::filesystem::path& p, const std::vector<float>& x)
{
    aurel::tooling::AudioData a;
    a.sampleRate = static_cast<std::uint32_t> (kRate);
    a.numChannels = 1;
    a.interleaved = x;
    return aurel::tooling::writeWavFloat32 (p, a);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 3 || std::string (argv[1]) != "--out")
    {
        std::fputs ("usage: aurel_juce_baseline --out <dir>\n", stderr);
        return 1;
    }
    const std::filesystem::path dir = argv[2];
    std::filesystem::create_directories (dir);
    juce::ScopedNoDenormals noDenormals;

    for (const int fc : { 100, 1000, 5000, 10000, 15000, 20000 })
    {
        if (! write (dir / ("juce_ladder_ir_" + std::to_string (fc) + ".wav"),
                     renderLadder (static_cast<float> (fc), 0.0f, 1e-4f, static_cast<int> (kRate))))
            return 2;
        if (! write (dir / ("juce_ladder_selfosc_" + std::to_string (fc) + ".wav"),
                     renderLadder (static_cast<float> (fc), 1.0f, 0.01f, static_cast<int> (kRate * 4))))
            return 2;
    }

    // SVF equivalence (same TPT algorithm; JUCE: float, AUREL: double).
    double maxDiff = 0.0, maxOut = 0.0;
    for (const int type : { 0, 1, 2 })
    {
        for (const double fc : { 100.0, 1000.0, 10000.0, 30000.0 })
        {
            for (const double q : { 0.5, 0.7071, 5.0 })
            {
                juce::dsp::StateVariableTPTFilter<float> j;
                j.prepare ({ kRate, static_cast<juce::uint32> (kBlock), 1 });
                j.setType (type == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                         : type == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                     : juce::dsp::StateVariableTPTFilterType::highpass);
                j.setCutoffFrequency (static_cast<float> (fc));
                j.setResonance (static_cast<float> (q));
                j.reset();

                aurel::dsp::StateVariableFilter a;
                a.prepare (kRate);
                a.setCutoffAndDamping (fc, 1.0 / q);

                std::mt19937 rng (1);
                std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
                for (int n = 0; n < static_cast<int> (kRate); ++n)
                {
                    const float x = noise (rng);
                    const double yj = j.processSample (0, x);
                    const auto o = a.process (x);
                    const double ya = type == 0 ? o.lp : type == 1 ? o.bp : o.hp;
                    maxDiff = std::max (maxDiff, std::abs (yj - ya));
                    maxOut = std::max (maxOut, std::abs (ya));
                }
            }
        }
    }
    std::printf ("svf_max_abs_difference %.3e (max output %.3f)\n", maxDiff, maxOut);

    // CPU: JUCE ladder.
    {
        juce::dsp::LadderFilter<float> f;
        f.prepare ({ kRate, static_cast<juce::uint32> (kBlock), 1 });
        f.setMode (juce::dsp::LadderFilterMode::LPF24);
        f.setCutoffFrequencyHz (2000.0f);
        f.setResonance (0.7f);
        f.setDrive (2.0f);
        f.reset();
        constexpr int total = 1 << 22;
        std::vector<float> buf (kBlock);
        double sink = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int start = 0; start < total; start += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
                buf[static_cast<std::size_t> (i)] = 2.0f * std::fmod ((start + i) * 110.0f / 96000.0f, 1.0f) - 1.0f;
            float* channels[] = { buf.data() };
            juce::dsp::AudioBlock<float> block (channels, 1, static_cast<size_t> (kBlock));
            f.process (juce::dsp::ProcessContextReplacing<float> (block));
            sink += buf[0];
        }
        const auto t1 = std::chrono::steady_clock::now();
        std::printf ("juce_ladder_ns_per_sample %.2f (sink %g)\n",
                     std::chrono::duration<double, std::nano> (t1 - t0).count() / total, sink);
    }
    return 0;
}
