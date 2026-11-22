#include "Engine/Engine.h"

#include "DSP/Core/Conversions.h"
#include "DSP/Core/Denormals.h"

#include <algorithm>

namespace aurel::engine
{

int Engine::oversamplingFactorFor (double hostSampleRate) noexcept
{
    if (hostSampleRate >= 88200.0 - 1.0)
        return 1;
    if (hostSampleRate >= 44100.0 - 1.0)
        return 2;
    return 4;
}

void Engine::prepare (double hostSampleRate, int maxBlockSize)
{
    factor = oversamplingFactorFor (hostSampleRate);
    stages = factor == 1 ? 0 : (factor == 2 ? 1 : 2);
    internalRate = hostSampleRate * factor;
    maxBlock = std::max (1, maxBlockSize);

    const auto busSize = static_cast<std::size_t> (maxBlock * factor);
    busLeft.assign (busSize, 0.0f);
    busRight.assign (busSize, 0.0f);

    // Stage s decimates from hostRate * 2^(stages - s) to half of that. Each
    // keeps 0..passband flat and rejects everything that would fold into it.
    const double passband = std::min (kPassbandEdgeHz, 0.4535 * hostSampleRate);
    for (int s = 0; s < stages; ++s)
    {
        const double inRate = hostSampleRate * static_cast<double> (1 << (stages - s));
        const double transition = (inRate / 4.0 - passband) / inRate;
        const auto design = dsp::halfband::design (kDecimatorAttenuationDb, transition);
        for (auto& channel : decimators)
            channel[static_cast<std::size_t> (s)].setDesign (design);
    }

    voices.prepare (internalRate);
    masterGain.prepare (hostSampleRate, kGainRampSeconds);
    gainInitialised = false;
}

void Engine::reset() noexcept
{
    voices.reset();
    for (auto& channel : decimators)
        for (auto& d : channel)
            d.reset();
    gainInitialised = false;
}

void Engine::decimate (int internalLength) noexcept
{
    int length = internalLength;
    for (int s = 0; s < stages; ++s)
    {
        auto& decL = decimators[0][static_cast<std::size_t> (s)];
        auto& decR = decimators[1][static_cast<std::size_t> (s)];
        length /= 2;
        // In place: output i reads inputs 2i and 2i+1, which are >= i.
        for (int i = 0; i < length; ++i)
        {
            const auto a = static_cast<std::size_t> (2 * i);
            busLeft[static_cast<std::size_t> (i)] = decL.process (busLeft[a], busLeft[a + 1]);
            busRight[static_cast<std::size_t> (i)] = decR.process (busRight[a], busRight[a + 1]);
        }
    }
}

void Engine::process (std::span<const Event> events, float* left, float* right, int numSamples,
                      const EngineParameters& params) noexcept
{
    if (numSamples <= 0 || ! isPrepared())
    {
        if (isPrepared())
            for (const auto& e : events)
                voices.handleEvent (e, params.voice);
        return;
    }

    const dsp::ScopedFlushDenormals noDenormals;

    const float targetGain = dsp::decibelsToGain (params.masterGainDb);
    if (! gainInitialised)
    {
        masterGain.setCurrentAndTarget (targetGain);
        gainInitialised = true;
    }
    else
    {
        masterGain.setTarget (targetGain);
    }

    const auto clampOffset = [numSamples] (int offset) {
        return std::clamp (offset, 0, numSamples - 1);
    };

    std::size_t nextEvent = 0;

    for (int chunkStart = 0; chunkStart < numSamples; chunkStart += maxBlock)
    {
        const int chunkEnd = std::min (numSamples, chunkStart + maxBlock);
        const int chunkLength = chunkEnd - chunkStart;
        const int internalLength = chunkLength * factor;

        std::fill_n (busLeft.data(), internalLength, 0.0f);
        std::fill_n (busRight.data(), internalLength, 0.0f);

        int pos = chunkStart; // host samples
        while (pos < chunkEnd)
        {
            while (nextEvent < events.size() && clampOffset (events[nextEvent].sampleOffset) <= pos)
                voices.handleEvent (events[nextEvent++], params.voice);

            int segmentEnd = chunkEnd;
            if (nextEvent < events.size())
                segmentEnd = std::min (chunkEnd, clampOffset (events[nextEvent].sampleOffset));

            const auto busOffset = static_cast<std::size_t> ((pos - chunkStart) * factor);
            voices.render (busLeft.data() + busOffset, busRight.data() + busOffset,
                           (segmentEnd - pos) * factor, params.voice);
            pos = segmentEnd;
        }

        decimate (internalLength);

        float* outL = left + chunkStart;
        if (right != nullptr)
        {
            float* outR = right + chunkStart;
            for (int i = 0; i < chunkLength; ++i)
            {
                const float g = masterGain.next();
                outL[i] = busLeft[static_cast<std::size_t> (i)] * g;
                outR[i] = busRight[static_cast<std::size_t> (i)] * g;
            }
        }
        else
        {
            for (int i = 0; i < chunkLength; ++i)
            {
                const float g = masterGain.next();
                const auto idx = static_cast<std::size_t> (i);
                outL[i] = 0.5f * (busLeft[idx] + busRight[idx]) * g;
            }
        }
    }

    while (nextEvent < events.size())
        voices.handleEvent (events[nextEvent++], params.voice);
}

} // namespace aurel::engine
