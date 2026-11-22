# Testing

Run everything with `./Scripts/test.ps1` (see `docs/development/building.md`).

## C++ (Catch2 v3, via CTest)

`Tests/` — single executable `aurel_tests`; each `TEST_CASE` is registered as its own CTest test.

| Area | File | What is verified |
|---|---|---|
| Unit / DSP | `Unit/DSP/ConversionsTests.cpp` | 12-TET reference pitches; note↔Hz round trip within 1e-9 semitone over notes -24…151; dB conversions and -inf handling |
| Unit / DSP | `Unit/DSP/DenormalsTests.cpp` | Subnormals occur without the guard; `ScopedFlushDenormals` flushes them and restores the previous mode |
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
| `tests/test_render_pipeline.py` | Runs `aurel_render` end to end at all six target sample rates: frequency error < 0.001 Hz, amplitude within 1e-6, THD+N < -130 dB, channels identical, silence exactly zero, invalid arguments rejected |

### Measured baseline (M0, 2026-11-22)

Reference sine from `aurel_render` (997 Hz, amplitude 0.5, float32 WAV):

| Rate | Freq error | THD | THD+N |
|---|---|---|---|
| 44.1 kHz | 4.9e-11 Hz | -170.1 dB | -153.7 dB |
| 48 kHz | 2.3e-11 Hz | -171.5 dB | -153.7 dB |
| 96 kHz | 7.7e-11 Hz | -174.7 dB | -153.8 dB |
| 192 kHz | 2.6e-11 Hz | -177.2 dB | -153.8 dB |

THD+N ≈ -154 dB is the float32 rounding floor for this signal, so the pipeline adds no measurable error of its own. This is the noise floor against which all later DSP measurements are read.
