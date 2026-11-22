#include "DSP/Core/Denormals.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using aurel::dsp::ScopedFlushDenormals;

namespace
{
    // volatile operands prevent compile-time evaluation, so the multiply runs
    // on the FPU with the current MXCSR mode.
    float produceSubnormal()
    {
        volatile float smallest = std::numeric_limits<float>::min(); // smallest normal
        volatile float factor = 0.25f;
        return smallest * factor;
    }
} // namespace

TEST_CASE ("Without the guard, subnormals are produced", "[dsp][denormals]")
{
    const float r = produceSubnormal();
    CHECK (std::fpclassify (r) == FP_SUBNORMAL);
}

TEST_CASE ("ScopedFlushDenormals flushes to zero and restores the mode", "[dsp][denormals]")
{
    if (! ScopedFlushDenormals::isSupported())
        SKIP ("FTZ/DAZ control not implemented on this architecture");

    {
        const ScopedFlushDenormals guard;
        CHECK (produceSubnormal() == 0.0f);
    }

    // Mode restored after the guard is destroyed.
    CHECK (std::fpclassify (produceSubnormal()) == FP_SUBNORMAL);
}
