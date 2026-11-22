#pragma once

#include <span>
#include <string>

namespace aurel::render
{

struct FilterRenderOptions
{
    std::string filter = "ladder";  // ladder | svf
    std::string solver = "linear";  // ladder: linear | newton
    int iterations = 2;             // ladder Newton iteration limit (production: 2)
    int tuning = 1;                 // ladder self-oscillation tuning correction 0 / 1
    std::string mode = "lp24";      // ladder: lp24 | lp12; svf: lp | bp | hp | notch
    double cutoff = 1000.0;
    double feedback = 0.0;          // ladder k (4 = self-oscillation threshold)
    double resonance = 0.0;         // svf 0..1
    double driveDb = 0.0;           // ladder
    double compensation = 0.0;      // ladder bass compensation 0..1
    double prefilterHz = 0.0;       // > 0: 4th-order Butterworth low-pass on the input before the filter
    std::string input = "saw";      // saw | sine | impulse
    double frequency = 110.0;       // input frequency
    double amplitude = 1.0;
    double sampleRate = 96000.0;    // the filter runs at this rate directly (no engine)
};

[[nodiscard]] bool isValidFilterOptions (const FilterRenderOptions& o);

/** Renders input -> filter into `out`. Returns false on invalid options. */
[[nodiscard]] bool renderFilter (const FilterRenderOptions& o, std::span<float> out);

/** Times the filters and prints a table. Returns the process exit code. */
int benchmarkFilters();

} // namespace aurel::render
