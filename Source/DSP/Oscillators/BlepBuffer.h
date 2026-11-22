#pragma once

// Overlap-add buffer for band-limiting corrections.
//
// Per output sample:  push(naive value)  ->  addStep / addCorner for every
// discontinuity inside the sample interval  ->  pop()  (delayed by the
// kernel half-span, so corrections can reach samples *before* the event).
//
// Event time f is in (0, 1]: the fraction of the interval between the
// previous and the current sample (f = 1 is exactly at the current sample).
// f = 0 is accepted too (kernels vanish at the support edge).

#include "DSP/Oscillators/BlepKernels.h"

#include <array>
#include <bit>
#include <cstdint>

namespace aurel::dsp
{

template <class Kernel>
class BlepBuffer
{
public:
    static constexpr int kLatency = Kernel::kHalfSpan;

    void reset() noexcept
    {
        ring.fill (0.0f);
        writeIndex = 0;
    }

    void push (float naive) noexcept { ring[slot (0)] += naive; }

    void addStep (float height, double f) noexcept
    {
        for (int j = -kHalfSpan; j < kHalfSpan; ++j)
            ring[slot (j)] += height * Kernel::blep (j + 1.0 - f);
    }

    void addCorner (float slopeDelta, double f) noexcept
    {
        for (int j = -kHalfSpan; j < kHalfSpan; ++j)
            ring[slot (j)] += slopeDelta * Kernel::blamp (j + 1.0 - f);
    }

    [[nodiscard]] float pop() noexcept
    {
        auto& out = ring[slot (-kLatency)];
        const float y = out;
        out = 0.0f;
        ++writeIndex;
        return y;
    }

private:
    static constexpr int kHalfSpan = Kernel::kHalfSpan;
    static constexpr std::uint32_t kRingSize = std::bit_ceil (static_cast<std::uint32_t> (2 * kHalfSpan + 1));
    static constexpr std::uint32_t kMask = kRingSize - 1;

    [[nodiscard]] std::size_t slot (int offset) const noexcept
    {
        return static_cast<std::size_t> ((writeIndex + static_cast<std::uint32_t> (offset)) & kMask);
    }

    std::array<float, kRingSize> ring {};
    std::uint32_t writeIndex = 0;
};

} // namespace aurel::dsp
