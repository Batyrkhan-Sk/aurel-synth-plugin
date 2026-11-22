#pragma once

// Renders an Engine over an absolute-time event list with a chosen block
// size, as a host would: events are split per block and re-based to each
// block's start.

#include "Engine/Engine.h"

#include <algorithm>
#include <vector>

namespace aurel::test
{

struct TimedEvent
{
    long long time = 0; // absolute sample position
    engine::Event event;
};

inline engine::Event noteOn (int note, float velocity = 1.0f, int channel = 0)
{
    engine::Event e;
    e.type = engine::EventType::NoteOn;
    e.note = static_cast<std::uint8_t> (note);
    e.channel = static_cast<std::uint8_t> (channel);
    e.value = velocity;
    return e;
}

inline engine::Event noteOff (int note, int channel = 0)
{
    engine::Event e;
    e.type = engine::EventType::NoteOff;
    e.note = static_cast<std::uint8_t> (note);
    e.channel = static_cast<std::uint8_t> (channel);
    return e;
}

inline engine::Event sustain (bool down)
{
    engine::Event e;
    e.type = engine::EventType::SustainPedal;
    e.value = down ? 1.0f : 0.0f;
    return e;
}

inline engine::Event simple (engine::EventType type)
{
    engine::Event e;
    e.type = type;
    return e;
}

struct StereoRender
{
    std::vector<float> left, right;
};

/** Renders `totalSamples` in blocks of `blockSize`. `events` must be sorted
    by time. Uses preallocated scratch so the process loop itself does not
    allocate. */
inline StereoRender renderBlocks (engine::Engine& eng, const std::vector<TimedEvent>& events,
                                  long long totalSamples, int blockSize,
                                  const engine::EngineParameters& params = {})
{
    StereoRender out;
    out.left.assign (static_cast<std::size_t> (totalSamples), 0.0f);
    out.right.assign (static_cast<std::size_t> (totalSamples), 0.0f);

    std::vector<engine::Event> blockEvents;
    blockEvents.reserve (events.size());

    std::size_t next = 0;
    for (long long start = 0; start < totalSamples; start += blockSize)
    {
        const auto n = static_cast<int> (std::min<long long> (blockSize, totalSamples - start));
        blockEvents.clear();
        while (next < events.size() && events[next].time < start + n)
        {
            auto e = events[next++].event;
            e.sampleOffset = static_cast<int> (std::max (0LL, events[next - 1].time - start));
            blockEvents.push_back (e);
        }
        eng.process (blockEvents, out.left.data() + start, out.right.data() + start, n, params);
    }
    return out;
}

} // namespace aurel::test
