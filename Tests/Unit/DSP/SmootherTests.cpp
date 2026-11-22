#include "DSP/Core/Smoother.h"

#include <catch2/catch_test_macros.hpp>

using aurel::dsp::LinearSmoother;

TEST_CASE ("LinearSmoother reaches the target exactly after the ramp", "[dsp][smoother]")
{
    LinearSmoother s;
    s.prepare (48000.0, 0.01); // 480 samples
    s.setCurrentAndTarget (0.0f);
    s.setTarget (1.0f);

    float prev = 0.0f;
    for (int i = 0; i < 479; ++i)
    {
        const float v = s.next();
        REQUIRE (v > prev); // monotonic
        REQUIRE (v < 1.0f);
        prev = v;
    }
    CHECK (s.next() == 1.0f);
    CHECK_FALSE (s.isSmoothing());
    CHECK (s.next() == 1.0f);
}

TEST_CASE ("LinearSmoother retargets mid-ramp from the current value", "[dsp][smoother]")
{
    LinearSmoother s;
    s.prepare (1000.0, 0.1); // 100 samples
    s.setCurrentAndTarget (0.0f);
    s.setTarget (1.0f);
    for (int i = 0; i < 50; ++i)
        (void) s.next();
    const float mid = s.getCurrent();
    s.setTarget (0.0f);
    CHECK (s.next() < mid); // continues from where it was, no jump
    for (int i = 0; i < 99; ++i)
        (void) s.next();
    CHECK (s.getCurrent() == 0.0f);
}

TEST_CASE ("LinearSmoother setCurrentAndTarget jumps without ramp", "[dsp][smoother]")
{
    LinearSmoother s;
    s.prepare (48000.0, 0.02);
    s.setCurrentAndTarget (0.5f);
    CHECK_FALSE (s.isSmoothing());
    CHECK (s.next() == 0.5f);
}
