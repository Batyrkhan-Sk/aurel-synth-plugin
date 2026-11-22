# Filters (M3a): design, solver decision and validation

Date: 2026-11-22. Code: `Source/DSP/Filters/`. Reproduce: `Scripts/analysis/m3_filter_measurements.py`, `m3_juce_comparison.py` (needs `aurel_juce_baseline`), `ladder_tuning_table.py`.

## Decisions

| # | Decision | Evidence |
|---|---|---|
| F1 | **Ladder solver: Newton–Raphson on the full implicit system, fixed 2 iterations**, starting from the linearised estimate, with backtracking and a non-finite fallback | 2 iterations match the converged solution to ≤ −77 dB in the hardest case (typically < −115 dB). The linearised one-step method deviates by up to **24 dB** in harmonic levels, its self-oscillation pitch wanders −27…+9 cents, and it aliases 5–17 dB more |
| F2 | **Self-oscillation tuning table** (offset vs k, 21 points) | Raw model is flat by an amount that depends only on k (−28 cents at k = 4.3, −101 at k = 6), not on the cutoff. Corrected: ±0.25 cents up to 3.5 kHz, ≤ 3.5 cents at 14 kHz |
| F3 | **Band-limit the ladder input to 20 kHz** (4th-order Butterworth, 2 SVF sections) | At the 96 kHz internal rate the oscillators contain 20–48 kHz harmonics that only exist because of oversampling. They intermodulate in the first tanh stage and fold into the audible band. The band limit improves default-drive aliasing from −43…−56 to **−98…−111 dBc** |
| F4 | **Custom ladder over `juce::dsp::LadderFilter`** | JUCE: cutoff off by +52…+411 cents (frequency-dependent), no self-oscillation below 15 kHz. AUREL: cutoff exact (0.00 cents), self-oscillates in tune. JUCE is 4× cheaper |
| F5 | **Custom SVF** (same TPT algorithm as `juce::dsp::StateVariableTPTFilter`) | Numerically equivalent to JUCE's (max difference 3.6×10⁻⁶, float rounding). Justified by the JUCE-free core (D8) and simultaneous LP/BP/HP/notch outputs with continuous morph, not by sound |

## Filters

**State-variable filter** (`StateVariableFilter.h`): 2-pole TPT (Zavalishin ch. 4; Simper's form). The cutoff is pre-warped, so cutoff and Q are exact at any frequency below Nyquist. Outputs LP, BP, HP; notch = LP + HP; `morph()` blends LP → unity-peak BP → HP → notch. Resonance 0…1 maps to damping k = 2(1 − r), with Q ≤ 200. Linear: it does not self-oscillate. The same class in HP mode is the series high-pass.

**Transistor ladder** (`LadderFilter.h`): Huovilainen's model (tanh on every stage's input and output) in TPT form. LP24 (y₄) and LP12 (y₂) outputs, feedback k (4 = self-oscillation threshold, max 6), bass compensation c ∈ [0, 1], drive. Small-signal it is exactly the linear ladder H₂₄ = (1 + ck)/((1 + s)⁴ + k). The input is scaled by drive × 0.5 into the model domain and scaled back at the output, so drive changes saturation, not pass-band level.

## Validation

**Small-signal response (C++ tests):** both filters match their analytic transfer functions within **0.01 dB** over 20 Hz–40 kHz, for cutoffs 100 Hz–30 kHz and every mode, resonance and compensation setting tested (both ladder solvers).

**Stability (C++ tests):** finite and bounded output for every combination of cutoff 20 Hz–43 kHz, k 0–6, drive 0.25–16, full-scale and +12 dB saw input, and a 10-octave audio-rate cutoff sweep at 1 kHz at maximum resonance (both filters). The plain Newton iteration failed to converge for g > 3.7 (cutoff ≥ 40 kHz at 96 kHz). This was found by the stability test and fixed with backtracking, verified with zero non-converged samples up to 43 kHz.

### Solver accuracy (110 Hz saw, full scale; difference to the converged solution)

| cutoff | k | drive | linearised | Newton-1 | Newton-2 |
|---|---|---|---|---|---|
| 300 | 0 | 0 dB | −69 dB | −166 dB | exact |
| 2000 | 3.0 | +24 dB | −27 dB (Δharm 4.1 dB) | −84 dB | −160 dB |
| 2000 | 3.9 | +12 dB | −19 dB (Δharm 5.4 dB) | −91 dB | −171 dB |
| 8000 | 3.0 | +12 dB | −23 dB (Δharm **11.5 dB**) | −61 dB | −122 dB |
| 8000 | 3.9 | +12 dB | −15 dB (Δharm **23.7 dB**) | −52 dB | −115 dB |
| 8000 | 3.9 | +24 dB | −21 dB (Δharm 13.2 dB) | −49 dB | −77 dB |

Δharm = largest level error of any harmonic above −60 dBc. Full table: 27 cases in `reports/m3/summary.md` (regenerate with the script).

### Self-oscillation pitch (k = 4.3, cents)

| cutoff | 110 | 440 | 1760 | 3520 | 7040 | 14080 |
|---|---|---|---|---|---|---|
| raw model | −28.07 | −28.07 | −28.01 | −27.83 | −27.14 | −24.71 |
| **corrected** | **−0.02** | **−0.02** | **+0.04** | **+0.23** | **+0.94** | **+3.45** |

C++ test across 88.2/96/192 kHz, k = 4.15/4.5/5.5, 55 Hz–7 kHz: worst 3.3 cents (7 kHz, k = 5.5, 88.2 kHz).

### THD vs drive (LP24, k = 0)

| drive | −12 dB | −6 dB | 0 dB | +6 dB | +12 dB | +24 dB |
|---|---|---|---|---|---|---|
| 220 Hz sine, cutoff 2 kHz | −56.5 | −44.5 | −32.3 | −20.4 | −12.6 | −10.7 |
| 1 kHz sine at cutoff 1 kHz | −78.2 | −66.5 | −55.7 | −47.3 | −43.4 | −42.5 |

At 0 dB drive the ladder adds audible warmth (2.4 % THD on a full-scale passband sine); −12 dB is near-clean. The final drive range and default are voiced in M3b.

### Aliasing (saw input, 96 kHz internal, worst in-band alias, dBc)

| input | cutoff | k | drive | linearised | Newton-2 | **Newton-2 + 20 kHz band limit** |
|---|---|---|---|---|---|---|
| 440 Hz | 5 kHz | 3.5 | 0 dB | −56.7 | −55.8 | **−106.2** |
| 1760 Hz | 10 kHz | 3.5 | 0 dB | −44.7 | −43.1 | **−97.9** |
| 3520 Hz | 20 kHz | 0 | 0 dB | −57.8 | −55.4 | **−101.2** |
| 880 Hz | 3 kHz | 4.2 | 0 dB | −56.0 | −54.4 | **−110.5** |
| 440 Hz | 5 kHz | 3.5 | +12 dB | −40.9 | −46.4 | −73.8 |
| 1760 Hz | 10 kHz | 3.5 | +12 dB | −33.6 | −33.9 | −57.4 |
| 440 Hz | 5 kHz | 3.5 | +24 dB | −32.8 | −48.8 | −54.1 |
| 1760 Hz | 10 kHz | 3.5 | +24 dB | −24.4 | −36.9 | −41.4 |

At default drive the ladder meets the −80 dBc target with 18–30 dB margin. **Heavy drive (+12…+24 dB) still aliases (−41…−74 dBc).** The tanh stages generate harmonics beyond the 96 kHz rate. At 192 kHz the +24 dB cases measure −54…−59 dBc. This feeds the M5 oversampling decision, together with the M2b sync/FM items.

### JUCE comparison (96 kHz, LP24)

| cutoff | cutoff error, cents (JUCE / AUREL) | self-oscillation error, cents (JUCE / AUREL) |
|---|---|---|
| 100 Hz | +410.6 / 0.00 | none / −0.0 |
| 1 kHz | +337.9 / 0.00 | none / +0.0 |
| 5 kHz | +317.8 / 0.00 | none / +0.5 |
| 10 kHz | +255.7 / 0.00 | none / +1.9 |
| 15 kHz | +165.0 / 0.00 | −152.3 / +3.9 |
| 20 kHz | +52.0 / 0.00 | −263.0 / +6.6 |

## CPU (Release, i5-11400H, 96 kHz, ns per sample)

| filter | case | ns/sample |
|---|---|---|
| ladder, linearised | k 2 / k 3.8 +12 dB / k 4.5 +24 dB | 74 / 94 / 141 |
| **ladder, Newton-2 (production)** | same | **179 / 242 / 393** |
| `juce::dsp::LadderFilter` | k 0.7, drive 2 | 47 |
| SVF | fixed / per-sample coefficients | 7 / 13 |

**Cost is the open issue.** At 96 kHz the production ladder takes 1.7–3.8 % of a core per voice. Restructuring (shared tanh values, fewer divisions, no residual re-check after the last iteration) already cut it by 36–45 %. Remaining M5 work: a fast rational tanh (`std::tanh` slows down for large arguments), float/SIMD processing across voices, and per-block coefficient interpolation.
