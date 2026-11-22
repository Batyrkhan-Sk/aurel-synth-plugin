# Testing

Run everything with `./Scripts/test.ps1` (see `docs/development/building.md`).

## C++ (Catch2 v3, via CTest)

`Tests/` — single executable `aurel_tests`; each `TEST_CASE` is registered as its own CTest test.

| Area | File | What is verified |
|---|---|---|
| Unit / DSP | `Unit/DSP/ConversionsTests.cpp` | 12-TET reference pitches; note↔Hz round trip within 1e-9 semitone over notes -24…151; dB conversions and -inf handling |
| Unit / DSP | `Unit/DSP/DenormalsTests.cpp` | Subnormals occur without the guard; `ScopedFlushDenormals` flushes them and restores the previous mode |
| Unit / DSP | `Unit/DSP/OscillatorTests.cpp` | BLEP/BLAMP kernels vanish at the support ends, give the midpoint at the step, and are odd/even symmetric; band-limited output is identical to the naive waveform away from discontinuities (polyblep2, sinc4, sinc8; saw/pulse/triangle); zero DC; pulse-width mean; peaks ≤ 1.2 (Gibbs ≈ 1.18 is correct); exact sine; no allocation |
| Unit / DSP | `Unit/DSP/SmootherTests.cpp` | Linear smoother lands exactly on target, is monotonic, retargets mid-ramp without jumps |
| Unit / engine | `Unit/Engine/MidiParserTests.cpp` | Note On/Off (incl. velocity-0 Note On), channel extraction, CC64 threshold, CC120/121/123-127, rejection of unsupported/short/SysEx messages |
| Engine | `Unit/Engine/EngineTests.cpp` | Silence without notes; pitch within 0.01 cent (55 Hz to 3.5 kHz); level vs velocity and master gain; sample-accurate events; **bit-identical output for block sizes 1/7/64/333/512/4096** (4096 > max block, exercises internal chunking); zero-length blocks still apply events; mono = L/R average; release to exact silence; FL Studio same-key overlap order; sustain pedal (incl. held keys on pedal-up); All Notes/Sound Off; voice stealing bound; gain smoothing; **no allocation in `process()`**; seeded MIDI fuzz (5 seeds × 1500 random blocks incl. empty and oversize blocks, pedal and All-Notes-Off): output finite and bounded, all 32 voices reached, zero voices and exact silence after releasing everything |
| Unit / tooling | `Unit/Tooling/WavFileTests.cpp` | Float WAV round trip is bit-exact; invalid input, garbage and truncated files rejected |
| Real-time safety | `RealtimeSafety/AllocationCounterTests.cpp` | The allocation counter sees zero for allocation-free code, detects new/delete, container growth and aligned new, observes only its own thread, and nests |

### Real-time safety instrumentation

`Tests/Support/AllocationCounter.{h,cpp}` replaces the global `operator new/delete` family in the test binary. A `ScopedAllocationCounter` counts heap allocations and deallocations made by the current thread while it is alive. Audio-path tests (from M1 on) wrap rendering in one and require zero of both.

Limitation: direct `malloc`/`free` calls are not intercepted.

## Python measurement harness

`Scripts/analysis/` — package `aurel_analysis` (`metrics.py`, `wav.py`), tests under `tests/`.

### Method (`metrics.py`)

* Kaiser window, β = 24 (sidelobes ≈ -220 dB, below float32 quantisation), float64 FFT.
* Component power = sum of |X|² over a ±12-bin band, normalised to mean-square (A²/2 for a sine).
* Frequency: peak of an 8x zero-padded spectrum with parabolic interpolation on log power.
* THD: harmonics 2…20 below Nyquist relative to the fundamental. THD+N: all bins except DC and fundamental bands, summed directly (no `total - fundamental` subtraction, which would limit the floor to about -160 dB).

### Tests

| File | Purpose |
|---|---|
| `tests/test_metrics.py` | Validates the metrics on synthetic signals with known answers: frequency error < 0.001 Hz (41 Hz–18 kHz, non-coherent); amplitude within 1e-6; float64 sine THD < -180 dB; injected -60/-80 dB harmonics measured within 0.01 dB; tanh saturation detected; THD+N tracks injected -100 dB noise within 0.5 dB |
| `tests/test_render_pipeline.py` | Runs `aurel_render` end to end at all six target sample rates: frequency error < 0.001 Hz, amplitude within 1e-6, THD+N < -130 dB, channels identical, silence exactly zero, invalid arguments rejected. **Engine note** (notes 45/69/93 at 44.1-192 kHz): pitch within 0.001 cent, amplitude 0.25 within 1e-5, THD+N < -130 dB |

| `tests/test_metrics.py` (alias metric) | Ideal additive saw shows no alias (< −180 dBc) and exact harmonics; a planted −70 dBc alias is measured within 0.05 dB; content above 20 kHz is ignored |
| `tests/test_oscillator_aliasing.py` | Production kernel (sinc6) worst in-band alias < −100 dBc and harmonic error < 0.1 dB at 88.2/96 kHz (saw, 10 % pulse, triangle; 440 Hz–C8); naive saw still detected as aliased |

| `tests/test_m3.py` | Self-oscillating ladder in tune (< 1 cent at 220/880/3520 Hz); band-limited ladder alias < −90 dBc at default drive; Newton-2 within −70 dB of the converged solution in the hardest case |
| `tests/test_m2b.py` | Complete engine (2× voice + decimator) alias < −110 dBc at 44.1/48 kHz; sync (saw/pulse/triangle) < −100 dBc; sine-carrier FM < −150 dBc; the two documented limitations (sine-slave sync, saw-carrier FM) must not regress |

M3 C++ additions: `Unit/DSP/FilterTests.cpp`: SVF and ladder small-signal response vs analytic transfer functions (±0.01 dB, all modes/solvers), stability matrix (cutoff 20 Hz–43 kHz × k 0–6 × drive 0.25–16 × +12 dB input), 10-octave audio-rate cutoff sweeps at maximum resonance, Newton convergence (zero non-converged samples up to 43 kHz), self-oscillation tuning (≤ 4 cents, 3 rates × 3 resonances × 55 Hz–7 kHz), unity DC gain with bass compensation, no allocation. A hidden `[.diag]` case prints near-Nyquist solver behaviour.

M2b C++ additions: `Unit/DSP/HalfbandTests.cpp` (decimator design validity, passband ±0.01 dB to 20 kHz, stopband < −100 dB, DC gain, stability), `Unit/DSP/SubNoiseTests.cpp` (sub locked to OSC1 at −1/−2 oct, exact sub sine, white-noise statistics and determinism, pink slope within ±0.5 dB of −3.01 dB/oct from 20 Hz to 20 kHz at 88.2/96/192 kHz, pink RMS = white RMS), and new cases in `OscillatorTests.cpp` and `EngineTests.cpp` (see [../dsp/oscillators-m2b.md](../dsp/oscillators-m2b.md)).

The full kernel comparison (`Scripts/analysis/oscillator_aliasing.py`) and `Scripts/analysis/m2b_measurements.py` are report generators, not tests; see [../dsp/oscillator-antialiasing.md](../dsp/oscillator-antialiasing.md).

## Plugin validation (pluginval)

`./Scripts/pluginval.ps1` runs Tracktion pluginval 1.0.4 at strictness 10 over sample rates 44.1/48/88.2/96/176.4/192 kHz and block sizes 32-2048: state save/restore, automation, parameter fuzzing, thread safety, bus layouts, editor open while processing.

**M1 result (2026-11-22): passes in both Debug (JUCE assertions on, none hit) and Release.**

Known gap: pluginval's *vst3 validator* step (Steinberg's official conformance validator) is **skipped**, because it needs a separately built validator binary. Planned as a follow-up.

## Manual DAW testing

FL Studio only, see [fl-studio-checklist.md](fl-studio-checklist.md).

| Milestone | Date | Result |
|---|---|---|
| M1 | 2026-11-22 | All checks passed (reported by the user; sustain-pedal check applies only with a pedal-equipped controller) |
| M2 | 2026-11-22 | Checks 14–24 passed (reported by the user) |

### Measured baseline (M0, 2026-11-22)

Reference sine from `aurel_render` (997 Hz, amplitude 0.5, float32 WAV):

| Rate | Freq error | THD | THD+N |
|---|---|---|---|
| 44.1 kHz | 4.9e-11 Hz | -170.1 dB | -153.7 dB |
| 48 kHz | 2.3e-11 Hz | -171.5 dB | -153.7 dB |
| 96 kHz | 7.7e-11 Hz | -174.7 dB | -153.8 dB |
| 192 kHz | 2.6e-11 Hz | -177.2 dB | -153.8 dB |

THD+N ≈ -154 dB is the float32 rounding floor for this signal, so the pipeline adds no measurable error of its own. This is the noise floor against which all later DSP measurements are read.
