#pragma once

// Linear parameter smoother: ramps from the current value to a new target
// over a fixed time. Used for gains, where a linear ramp is click-free and
// its duration is predictable. (Log-domain smoothing for cutoff and pitch
// comes with those parameters.)

#include <algorithm>
#include <cmath>

namespace aurel::dsp
{

class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        rampLength = std::max (1, static_cast<int> (std::lround (sampleRate * rampSeconds)));
        setCurrentAndTarget (target);
    }

    /** Jumps immediately, without a ramp (e.g. on prepare / reset). */
    void setCurrentAndTarget (float value) noexcept
    {
        current = target = value;
        remaining = 0;
    }

    void setTarget (float value) noexcept
    {
        if (value == target)
            return;
        target = value;
        remaining = rampLength;
        step = (target - current) / static_cast<float> (remaining);
    }

    [[nodiscard]] float next() noexcept
    {
        if (remaining > 0)
        {
            --remaining;
            current = remaining == 0 ? target : current + step; // land exactly on target
        }
        return current;
    }

    /** Advances n samples at once (control-rate use); returns the new value. */
    float skip (int n) noexcept
    {
        if (remaining > 0 && n > 0)
        {
            const int steps = std::min (n, remaining);
            remaining -= steps;
            current = remaining == 0 ? target : current + step * static_cast<float> (steps);
        }
        return current;
    }

    [[nodiscard]] bool isSmoothing() const noexcept { return remaining > 0; }
    [[nodiscard]] float getCurrent() const noexcept { return current; }
    [[nodiscard]] float getTarget() const noexcept { return target; }

private:
    float current = 0.0f;
    float target = 0.0f;
    float step = 0.0f;
    int remaining = 0;
    int rampLength = 1;
};

} // namespace aurel::dsp
