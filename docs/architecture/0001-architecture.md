# ADR 0001 — AUREL Architecture

| | |
|---|---|
| **Status** | Approved 2026-11-22 |
| **Scope** | Overall synthesis, DSP, engine, plugin, testing and roadmap |
| **Nature** | Design document. It describes the *planned* architecture. What is actually implemented is tracked in the milestone table (§10) and in the code. |

Tags: **[E]** established practice / literature · **[A]** engineering assumption, to be verified by measurement · **[X]** experimental, decided by measurement.

---

## 1. Synthesis approach

**Primary engine: virtual analog subtractive** [E], with three low-cost hybrid extensions:

1. Linear through-zero FM / phase modulation OSC1 → OSC2.
2. Hard sync OSC2 ← OSC1.
3. Ring modulation OSC1 × OSC2 as a mixer input.

Rejected for v1: wavetable engine (high UX and table-management cost, weak fit to the analog identity; the oscillator interface stays open for a v2 addition), multi-operator FM (UX cost, aliasing), full hybrid (complexity).

## 2. Signal flow

```
PER VOICE (mono; internal rate = 2x base at 44.1/48 kHz, 1x at >= 88.2 kHz)
 OSC1 --+-----------------+
        | sync / FM       |
 OSC2 <-+-----------------+
 OSC3 --------------------+--> MIXER --> [HPF] --> FILTER (Ladder | SVF) --> VCA --> PAN --+
 SUB  --------------------+    levels,              drive lives here                      |
 NOISE -------------------+    ring 1x2, noise                                            |
   mod: AmpEnv, FiltEnv, ModEnv, LFO1, LFO2, velocity, key, MW, AT, PB, drift             |
                                                                                          v
GLOBAL (stereo)  sum voices -> halfband decimator -> DC block -> DRIVE -> CHORUS -> DELAY -> REVERB -> WIDTH -> OUT
```

The voice path is mono up to the pan stage (halves per-voice filter cost). Stereo comes from per-voice pan, unison spread and effects. [A]

## 3. Oscillators (decision D2)

* **3 oscillators + sub (locked to OSC1, -1/-2 oct, square/sine) + noise (white/pink, seeded).**
* Waveforms: sine, triangle, saw, pulse with continuous PW. Waveform switches preserve phase; the step is BLEP-corrected.
* Pitch handled in semitones; octave / semi / fine / key-track.
* Phase: free-running or reset with start phase.
* Drift: slow random walk per oscillator + per-voice static tolerances, scaled by a global *Analog* amount, deterministic per voice index (seeded).
* No waveform morphing in v1.

**D2 requirements (from review):**
* Oscillators whose level is 0 and that are not a sync/FM source are not processed.
* Mixer headroom rule: one oscillator at 0 dB always yields the same nominal filter input level, independent of how many oscillators are enabled. Covered by a test.
* OSC3 gets a compact UI panel.
* Sync/FM pairs are fixed (OSC1 → OSC2) by design.

**Anti-aliasing: decided 2026-11-22 (M2a): `SincBlep<6>`**, a tabulated Kaiser-windowed-sinc BLEP/BLAMP. Worst in-band alias ≤ −110 dBc at the internal rate; 2-point PolyBLEP measured −42 to −53 dBc and was rejected. Evidence and CPU data: [docs/dsp/oscillator-antialiasing.md](../dsp/oscillator-antialiasing.md). The same measurement confirms D4 (2× internal rate): no kernel meets the target at 1× 44.1/48 kHz.

## 4. Filters

Custom implementations, justified by measurement against JUCE's `dsp::LadderFilter` and `dsp::StateVariableTPTFilter`, which serve as test baselines.

| Model | Implementation |
|---|---|
| Ladder (primary) | 4-pole ZDF/TPT transistor ladder, per-stage nonlinearity; **Newton–Raphson, 2 iterations (decided M3a)**; 12/24 dB taps; bass-compensated resonance; self-oscillation with measured tuning correction; input band-limited to 20 kHz. See [filters-m3.md](../dsp/filters-m3.md) |
| Multimode SVF | 2-pole TPT SVF, continuous LP→BP→HP→Notch, optional saturating integrators |
| Series HPF | Resonant 2-pole TPT HP per voice ahead of the main filter |
| Diode ladder, OTA | Deferred; filter-model slot reserved |

Cutoff is modulated in octaves. Validation: small-signal response vs analytic H(z), self-oscillation pitch tracking (cents), THD vs drive, stability matrix (max resonance × max drive × audio-rate cutoff modulation × all sample rates).

## 5. Engine

### 5.1 Layers

| Layer | Contents | Depends on |
|---|---|---|
| `aurel_dsp` (no JUCE) | oscillators, filters, envelopes, LFOs, smoothers, oversampling, effects, math | C++ std |
| `aurel_engine` (no JUCE) | Voice, VoiceManager, ModMatrix, Engine (events in → stereo out) | `aurel_dsp` |
| `AUREL` plugin | AudioProcessor, APVTS layout, MIDI adapter, state, presets, UI | engine + JUCE |
| `aurel_render` | offline renders and benchmarks | engine |
| `aurel_tests` | Catch2 tests | engine, tooling |

Dependencies point one way: UI → Plugin → Engine → DSP. No virtual dispatch in per-sample paths.

### 5.2 Voices (decision D7)

Custom voice manager (not `juce::Synthesiser`):
* Fixed 32-voice pool allocated in `prepare`; user polyphony 1–32, default 16 (final numbers from M5 benchmarks).
* Sample-accurate MIDI (blocks split at event timestamps).
* Steal order: released voices (quietest first), then oldest held; 2–5 ms steal fade.
* Mono/legato with last-note priority stack; glide in pitch domain, constant time or rate, optional legato-only.
* Sustain pedal, all-notes-off, all-sound-off, reset.
* **Unison by voice stacking** (1–8 voices per note), each with its own filter.

**D7 requirements (from review):**
* Events are matched to voices by voice/note ID, never by pitch alone. Explicit tests for: retrigger of a releasing note, note-off for a stolen voice, pedal + steal, velocity-0 note-on, FL Studio-style overlapping same-key notes at the same timestamp. Plus seeded MIDI fuzz tests.
* Stack-aware stealing: a unison stack is always stolen as a unit.
* Unison gain compensation of 1/√N, verified by RMS tests.
* Per-patch phase mode (random / reset) so bass transients can stay consistent.
* Sub oscillator plays only in the centre voice of a stack; optional mono-bass (low-cut on the unison spread).
* Open item for M5: if benchmarks show pads too constrained, add oscillator-internal unison as a second mode or raise the pool to 64 voices.

### 5.3 Modulation

Hardwired core (filter env → cutoff, key → cutoff, velocity → amp/filter, mod wheel → LFO1 vibrato depth, pitch bend → pitch) plus a **16-slot matrix** (source, destination, bipolar amount, optional *via* scaler) stored as fixed arrays with enum dispatch. Destinations use natural units (semitones, octaves, linear).

* 3 ADSR envelopes (Amp, Filter, Mod) with RC-like curves and per-stage curve control.
* 2 LFOs: sine, triangle, saw, square, S&H, smooth random; rate or host-tempo sync; phase; delay/fade-in; per-voice retrigger or free-running global.
* Sources: velocity, channel/poly aftertouch, mod wheel, pitch bend, key, 2 assignable CCs.
* Update rate [X]: envelopes and VCA per sample; matrix every 16–32 internal samples with linear ramps, benchmarked against per-sample evaluation.

### 5.4 Effects

Fixed order in v1: Drive (4x oversampled) → Chorus/Ensemble → Delay → Reverb (custom modulated FDN) → Width (in master). Phaser and compressor deferred. Concrete classes behind `prepare / reset / process / getLatency`; virtual dispatch per block only.

### 5.5 Parameters

* APVTS owns all host parameters; stable string IDs + version hint; IDs never change after release. Matrix slots are parameters.
* Audio thread reads cached `std::atomic<float>*`; the plugin builds a plain engine parameter snapshot per block.
* Smoothing: cutoff/pitch in log domain; gains linear ramp ~20 ms; resonance/drive one-pole; discrete switches apply immediately with phase/state preserved.

## 6. Real-time strategy

* No allocation after `prepareToPlay`; no locks, file I/O, `std::function` or logging in the audio path; FTZ/DAZ per block.
* UI/host → audio: APVTS atomics + per-block snapshot; commands (panic, preset fade) via SPSC FIFO.
* Audio → UI: `AbstractFifo` ring for scope data; atomics for meters / live modulation readback.
* Preset application: engine fades out, applies, fades in.
* Host robustness: `numSamples == 0`; blocks larger than the prepared size processed in chunks; repeated `prepareToPlay`; velocity-0 note-on; `get/setStateInformation` from non-message threads.
* Enforced by tests: the test binary replaces global `operator new/delete` and counts heap activity on the audio thread (implemented in M0, `Tests/Support/AllocationCounter`).

## 7. Audio quality strategy

* Voices run at ~88–96 kHz internally; the summed voice bus is decimated once by a polyphase IIR allpass halfband (low latency, ~-100 dB stopband). Drive effect oversamples separately (4x). Quality setting (1x/2x/4x) decided after benchmarks. [A]
* Gain staging: oscillators nominal ±1, normalised mixer, documented headroom per stage, drive control sets push into nonlinearity.
* DC blocked after voice sum (~5 Hz) and in delay feedback; intra-voice DC preserved intentionally.
* NaN/Inf guards in debug/test builds; cutoff clamped relative to sample rate; double-precision phase where tuning requires it.

## 8. Technology

| Item | Choice |
|---|---|
| Language | C++20 |
| JUCE | **9.0.6**, git submodule (shallow), pinned. Dual-licensed AGPLv3 / commercial JUCE 9 EULA. |
| Build | CMake ≥ 3.22, `CMakePresets.json`, Visual Studio 17 2022 x64 generator |
| Flags | `/W4 /WX /permissive- /utf-8 /Zc:__cplusplus`, default `/fp:precise`, static runtime (`/MT`) |
| SIMD | SSE2 baseline; AVX2 only after profiling |
| Formats | VST3 + Standalone |
| Tests | Catch2 v3.16.1 + CTest; Python 3.11 (NumPy/SciPy/Matplotlib, pinned); pluginval |

**Licensing constraint:** commercial release is a possible future goal. No GPL/AGPL third-party code except JUCE itself (whose licence path is chosen at release). Every dependency is listed in `docs/development/third-party.md`.

## 9. Testing & DAW validation

Unit, integration, MIDI fuzz, regression (spectral/RMS fingerprints with tolerances, not bit-exact), real-time safety, performance (`aurel_render --bench`), pluginval (strictness 10).

**DAW coverage: FL Studio only** (the only host available). pluginval covers generic host behaviour. REAPER (free evaluation) is an optional second host if it becomes available.

## 10. Roadmap

| # | Milestone | Done when | Status |
|---|---|---|---|
| M0 | Foundation | One command configures, builds and tests | **Done** 2026-11-22 |
| M1 | Minimal VST3 (poly sine, MIDI, APVTS) | pluginval passes; loads and plays in FL Studio | **Done** 2026-11-22: pluginval strictness 10 (Debug + Release); FL Studio checklist passed |
| M2 | Measurement harness + oscillators | Naive aliasing detected; chosen method meets target; tuning < 0.1 cent | **Done** 2026-11-22: end-to-end alias ≤ −116.9 dBc, tuning < 0.01 cent ([M2b report](../dsp/oscillators-m2b.md)); FL Studio checklist passed. Open for M5: sine-sync / saw-FM aliasing at high notes |
| M3 | Filters | Response, stability, self-oscillation targets; beats JUCE baselines on measured criteria | **M3a done** 2026-11-22 (DSP, solver decision, validation, JUCE comparison: [filters-m3.md](../dsp/filters-m3.md)); M3b (voice integration, parameters, FL check) next |
| M4 | Voices + modulation | Fuzz tests clean; click-free steals; playable checkpoint | Planned |
| M5 | Quality + performance | Benchmark report; polyphony/oversampling decided from data | Planned |
| M6 | Effects | Per-effect tests + CPU report | Planned |
| M7 | UI | Resizable, DPI-correct, thread-safe | Planned |
| M8 | Presets + DAW | Corrupt/old presets load safely; FL Studio checklist passes | Planned |
| M9 | Release | All validation green | Planned |

## 11. References

* V. Zavalishin, *The Art of VA Filter Design*, rev. 2.1.2, 2018.
* A. Huovilainen, "Non-linear digital implementation of the Moog ladder filter", DAFx 2004.
* S. D'Angelo, V. Välimäki, "An improved virtual analog model of the Moog ladder filter", ICASSP 2013.
* V. Välimäki, A. Huovilainen, "Antialiasing oscillators in subtractive synthesis", IEEE Signal Processing Magazine, 2007.
* V. Välimäki, J. Pekonen, J. Nam, "Perceptually informed synthesis of bandlimited classical waveforms using integrated polynomial interpolation", JASA, 2012.
* E. Brandt, "Hard sync without aliasing", ICMC 2001.
* T. Stilson, J. Smith, "Alias-free digital synthesis of classic analog waveforms", ICMC 1996.
* J.-M. Jot, A. Chaigne, "Digital delay networks for designing artificial reverberators", AES 1991.
* J. Dattorro, "Effect design, part 1: reverberator and other filters", JAES 1997.
