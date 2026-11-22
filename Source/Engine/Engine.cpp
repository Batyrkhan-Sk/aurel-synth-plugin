#include "Engine/Engine.h"

#include "DSP/Core/Conversions.h"
#include "DSP/Core/Denormals.h"

#include <algorithm>

namespace aurel::engine
{

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    maxBlock = std::max (1, maxBlockSize);
    busLeft.assign (static_cast<std::size_t> (maxBlock), 0.0f);
    busRight.assign (static_cast<std::size_t> (maxBlock), 0.0f);
    voices.prepare (sampleRate);
    masterGain.prepare (sampleRate, kGainRampSeconds);
    gainInitialised = false;
}

void Engine::reset() noexcept
{
    voices.reset();
    gainInitialised = false;
}

void Engine::process (std::span<const Event> events, float* left, float* right, int numSamples,
                      const EngineParameters& params) noexcept
{
    if (numSamples <= 0 || ! isPrepared())
    {
        if (isPrepared())
            for (const auto& e : events)
                voices.handleEvent (e);
        return;
    }

    const dsp::ScopedFlushDenormals noDenormals;

    const float targetGain = dsp::decibelsToGain (params.masterGainDb);
    if (! gainInitialised)
    {
        masterGain.setCurrentAndTarget (targetGain); // no fade-in on first block
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

        std::fill_n (busLeft.data(), chunkLength, 0.0f);
        std::fill_n (busRight.data(), chunkLength, 0.0f);

        int pos = chunkStart;
        while (pos < chunkEnd)
        {
            while (nextEvent < events.size() && clampOffset (events[nextEvent].sampleOffset) <= pos)
                voices.handleEvent (events[nextEvent++]);

            int segmentEnd = chunkEnd;
            if (nextEvent < events.size())
                segmentEnd = std::min (chunkEnd, clampOffset (events[nextEvent].sampleOffset));

            const int busOffset = pos - chunkStart;
            voices.render (busLeft.data() + busOffset, busRight.data() + busOffset, segmentEnd - pos);
            pos = segmentEnd;
        }

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

    // Events clamped to the last sample are consumed above; anything left
    // (only possible with unsorted input) is applied now so it is never lost.
    while (nextEvent < events.size())
        voices.handleEvent (events[nextEvent++]);
}

} // namespace aurel::engine
