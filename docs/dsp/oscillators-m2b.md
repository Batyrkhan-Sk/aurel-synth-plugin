# Oscillator section and oversampled voice (M2b)

Date: 2026-11-22. Builds on [oscillator-antialiasing.md](oscillator-antialiasing.md) (kernel choice: `SincBlep<6>`).

## What was implemented

```
OSC1 --+--> sync (reset at OSC1 wrap) --+
       +--> linear through-zero FM -----+--> OSC2 --+
       +--> SUB (-1/-2 oct, square/sine)            +--> MIXER --> gate env --> voice out
OSC3 -----------------------------------------------+
NOISE (white / pink) -------------------------------+
```

| Part | File | Design |
|---|---|---|
| Oscillator | `Source/DSP/Oscillators/BandlimitedOscillator.h` | Events found as phase crossings of breakpoints, in either direction. Steps flip sign when the phase runs backwards; corners scale with \|increment\|. Hard sync = reset at the master's sub-sample wrap time with step + slope correction. Waveform switch = step + slope correction at the sample. PWM: the pulse edge is where *phase − pw(t)* crosses 0, with pw moving linearly across the sample. |
| Correction buffer | `.../BlepBuffer.h` | Shared overlap-add ring used by all band-limited sources. |
| Sub-oscillator | `.../SubOscillator.h` | Phase derived from OSC1 (wrap count mod 2 or 4 + OSC1 phase), so it can never drift; square edges land exactly on OSC1 wraps and are BLEP-corrected. |
| Noise | `.../Noise.h` | White: xorshift32, uniform, RMS = 1/√3 (equal to a full-scale saw). Pink: 9–10 first-order shelving sections, poles at 5 Hz·3ⁱ, zeros at pole·√3, matched-z per sample rate. Normalised to white RMS. |
| Decimator | `Source/DSP/Oversampling/Halfband.h` | Polyphase IIR half-band (two all-pass chains), coefficients from the elliptic-function design for 110 dB stopband and a 20 kHz passband edge. One multiply per coefficient per output sample. |
| Voice / engine | `Source/Engine/Voice.*`, `Engine.*` | Voices run at ≥ 88.2 kHz (2× for 44.1/48 kHz, 4× below 44.1 kHz, 1× at ≥ 88.2 kHz). The summed stereo bus is decimated once per block. |
| Parameters | `Source/Plugin/ParameterLayout.cpp` | 3 × (wave, octave, semitone, fine, pulse width, level), OSC2 sync and FM, sub level/wave/octave, noise level/colour. |

**Mixer rule (D2):** every source has a fixed gain equal to its level, with no normalisation by the number of active sources. OSC2, OSC3, sub and noise are not processed while their level is 0 and not ramping. OSC1 always runs because it drives sync, FM and the sub.

**FM depth taper:** the host parameter (0–100 %) maps to index = (x/100)² × 8. The quadratic taper gives fine control of subtle FM.

## Measurements

All values: worst single alias component in 20 Hz–20 kHz, relative to the fundamental (`aurel_analysis.metrics.alias_analysis`). Reproduce with `Scripts/analysis/m2b_measurements.py`.

### Decimator (C++ test, exact-frequency DFT)

| | 44.1 kHz host | 48 kHz host |
|---|---|---|
| Passband 50 Hz–20 kHz | within ±0.01 dB | within ±0.01 dB |
| Stopband (everything that folds into 0–20 kHz) | < −100 dB | < −100 dB |
| DC gain | 1.000000 | 1.000000 |

### Complete engine (oscillator → 2× bus → decimator → host rate)

| host rate | wave | A2 | A4 | A6 | A7 | C8 |
|---|---|---|---|---|---|---|
| 44.1 kHz | saw | −145.8 | −134.3 | −123.3 | −117.3 | −116.9 |
| 44.1 kHz | pulse | −146.1 | −134.3 | −124.9 | −118.5 | −120.2 |
| 44.1 kHz | triangle | −160.6 | −165.0 | −156.8 | −144.9 | −144.7 |
| 48 kHz | saw | −147.1 | −135.1 | −124.5 | −118.5 | −119.7 |
| 48 kHz | pulse | −147.0 | −135.2 | −127.0 | −118.5 | −119.7 |
| 48 kHz | triangle | −172.7 | −166.4 | −159.4 | −145.4 | −146.1 |

**Target (−80 dBc) met with ≥ 37 dB margin.** Pitch is exact to < 0.01 cent at all host rates (C++ test, 32–192 kHz).

### Hard sync (OSC2 slave, 96 kHz)

| slave | ratio | 220 Hz | 880 Hz | 1760 Hz | 3520 Hz |
|---|---|---|---|---|---|
| saw | 1.5 | −137.3 | −125.2 | −124.6 | −118.5 |
| saw | 2.37 | −124.8 | −117.2 | −111.8 | −111.0 |
| saw | 4.9 | −103.8 | −92.7 | −90.1 | −86.8 |
| pulse | 4.9 | −98.7 | −87.6 | −85.0 | −81.9 |
| triangle | 4.9 | −128.7 | −115.0 | −106.4 | −96.1 |
| **sine** | 2.37 | −134.7 | −102.7 | −85.1 | **−66.8** |
| **sine** | 4.9 | −101.8 | −66.9 | −49.1 | **−30.7** |

### Through-zero linear FM (OSC2 carrier, sine modulator at the carrier frequency, 96 kHz)

| carrier | depth | 220 Hz | 880 Hz | 1760 Hz |
|---|---|---|---|---|
| sine | 0.5 / 1.5 / 4 | ≤ −169 | ≤ −165 | ≤ −164.8 |
| triangle | 4 | −121.9 | −86.1 | −71.2 |
| **saw** | 1.5 | −78.1 | **−52.5** | **−38.8** |
| **pulse** | 1.5 | −81.3 | **−55.9** | **−42.5** |

### CPU (Release, i5-11400H, 48 kHz host, 512-sample blocks, all sources on, sync + FM)

| voices | % of one core | % per voice |
|---|---|---|
| 1 | 2.6 | 2.6 |
| 16 | 24.0 | 1.5 |
| 32 | 38.4 | 1.2 |

Reproduce with `aurel_render --bench engine`. This is the baseline for M5 optimisation. The filter (M3) will add to it.

## Known limitations (decision deferred to M5, with this data)

1. **Sine-slave hard sync at high slave frequencies.** A sine reset is discontinuous in *every* derivative. The kernel corrects value (BLEP) and slope (BLAMP); curvature and higher remain. Clean at moderate settings; degrades when the *synced slave* is high (−31 dBc with the slave at 17 kHz). Saw, pulse and triangle slaves are unaffected: their higher derivatives are zero.
2. **FM of saw/pulse carriers at high notes.** The carrier's harmonics already extend close to the internal Nyquist; FM spreads every harmonic into sidebands far beyond it. No band-limited oscillator can prevent this. Sine and triangle carriers are clean.

Both are addressed by the same remedy: a higher oversampling factor for OSC2 when sync/FM is active. That is an oversampling/CPU trade-off, which ADR 0001 assigns to M5. Regression tests keep both from getting worse in the meantime.

## Tests

* C++: backward-phase, hard-sync (against the analytic synced waveform: 12,772 comparisons), waveform-switch, PWM and wrap-time tests in `OscillatorTests.cpp`; sub/noise/pink-slope in `SubNoiseTests.cpp`; decimator passband/stopband/stability in `HalfbandTests.cpp`; engine oversampling factor, exact pitch at 7 host rates, bit-exact sample-accurate timing through the oversampled path in `EngineTests.cpp`.
* Python: `tests/test_m2b.py` guards the measured values above.
