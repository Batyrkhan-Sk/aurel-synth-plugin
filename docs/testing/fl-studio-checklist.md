# FL Studio manual checklist

FL Studio is AUREL's primary host and the only one available for manual testing. Run this list for every milestone that touches the plugin. Record the FL Studio version, buffer size and sample rate you tested with.

## Making the plugin visible to FL Studio (no admin rights needed)

1. Build Release: `./Scripts/build.ps1 -Config Release`
2. In FL Studio: **Options → Manage plugins → Plugin search paths → Add path**, and add:
   `<repo>\build\vs2022\Source\Plugin\AUREL_artefacts\Release\VST3`
3. Click **Find installed plugins** (or **Find more plugins**). *AUREL* (vendor *AUREL*) should appear under *Generators*.

Rebuilding overwrites the plugin in place. Close FL Studio first: Windows locks loaded plugin DLLs.

Alternatively, copy `AUREL.vst3` (the whole folder) to `C:\Program Files\Common Files\VST3\` (requires admin).

## M1 checks

| # | Check | Expected |
|---|---|---|
| 1 | Add AUREL to the Channel Rack | Loads without error; generic editor shows **Master Volume** |
| 2 | Play notes from the computer keyboard / MIDI keyboard | Clean sine tones at the right pitch; chords work |
| 3 | Hold 40+ notes (e.g. a big piano-roll chord) | No crash; old notes are replaced (voice stealing) |
| 4 | Piano roll: two touching notes on the same key, played back | Both notes sound; the second is not cut short |
| 5 | Sustain pedal (if available, or CC64 automation in piano roll) | Notes hold while pedal is down, release when up |
| 6 | Stop playback with notes held | No stuck notes |
| 7 | Move **Master Volume** while playing | Smooth change, no clicks/zipper noise |
| 8 | Automate Master Volume (right-click → Create automation clip), play back | Follows the clip smoothly |
| 9 | Save project, close FL Studio, reopen project | AUREL loads with the saved Master Volume |
| 10 | Change sample rate (Options → Audio settings) 44.1 → 48 → 96 kHz | Pitch stays correct, no glitches |
| 11 | Change buffer size (small, e.g. 128 samples, then large) | No crackles beyond normal CPU limits; pitch unchanged |
| 12 | Export WAV (File → Export) of a pattern | Render matches realtime playback |
| 13 | Delete AUREL from the channel rack, add it again | No crash |

## M2 checks (oscillators)

The generic editor now shows parameters grouped by Oscillator 1–3, Sub, Noise and Master.

| # | Check | Expected |
|---|---|---|
| 14 | Play the init sound | Bright saw tone (Osc 1 = Saw at 100 %) |
| 15 | Hold a note and switch **Osc 1 Wave** through Sine → Triangle → Saw → Pulse | Timbre changes with no clicks |
| 16 | **Osc 1 Wave = Pulse**, move **Osc 1 Pulse Width** slowly while holding a note | Smooth, hollow-to-nasal sweep, no crackle |
| 17 | **Osc 2 Level** 100 %, **Osc 2 Fine** +7 ct | Slow beating / thick detuned sound |
| 18 | **Osc 2 Sync** on, Osc 1 Level 0, sweep **Osc 2 Semitone** while holding a note | Classic "sync" vocal sweep; pitch stays at the played note |
| 19 | **Osc 2 FM** 20–50 % with Osc 2 audible | Metallic / bell-like tones |
| 20 | **Sub Level** 100 %, try **Sub Octave** -1 / -2 and **Sub Wave** Square / Sine | Deeper bass one / two octaves below |
| 21 | **Noise Level** up, **Noise Color** White vs Pink | Hiss; pink sounds darker |
| 22 | Saw, play very high notes (C7–C8) | Pure high tones, no whistling or "inharmonic" tones under them |
| 23 | All three oscillators on, 8-note chords | No crackle; FL's CPU meter stays reasonable |
| 24 | Repeat 14–18 at 44.1, 48 and 96 kHz | Same pitch and character |

Known, expected: Osc 2 **Sine** + Sync at very high Semitone settings on high notes, and Osc 2 **Saw/Pulse** + strong FM on high notes, can sound slightly gritty. Documented; addressed in M5.

## M3 checks (filters)

New parameter group **Filter**. Init sound: Ladder 24, cutoff 20 kHz (open), resonance 0.

| # | Check | Expected |
|---|---|---|
| 25 | Hold a saw chord, sweep **Filter Cutoff** from 20 kHz down to ~100 Hz | Smooth, even "wah" sweep down to a deep, round tone; no zipper noise or steps |
| 26 | **Filter Resonance** 0 → 80 % while sweeping cutoff | Increasingly vocal, whistling peak |
| 27 | **Resonance 100 %**, **Osc 1 Level 0**, **Key Tracking 100 %**, cutoff ~260 Hz, play a melody | The filter itself sings a sine-like tone that follows the keyboard in tune |
| 28 | **Filter Drive** -12 → +24 dB on a bass note (cutoff ~800 Hz, resonance ~50 %) | From clean (and quieter) to thick and saturated; at +24 dB clearly distorted |
| 29 | **Bass Compensation** 0 % vs 100 % at resonance ~70 % | 0 %: low end thins as resonance rises (classic ladder); 100 %: bass stays full |
| 30 | While holding a note, switch **Filter Type** Ladder 24 → Ladder 12 → SVF → Off | Character changes; no clicks or pops at the switch |
| 31 | **SVF**: sweep **SVF Mode** LP → BP → HP → Notch | Continuous morph through the four responses |
| 32 | **High-Pass Cutoff** up to ~500 Hz on a bass sound; back to minimum ("Off") | Low end removed; at "Off" the full bass returns |
| 33 | Automate Filter Cutoff with an automation clip | Follows smoothly |
| 34 | Play 8-note chords, watch FL's CPU meter | Works; note the CPU figure (see below) |

**Known, expected (M5):** CPU is high, about 3–4 % of one core per sounding voice. Big pads with 16 or more voices are heavy, and 32 voices can overload a core. Very high notes with an open, resonant ladder, or strong drive, can add slight grit. Both are scheduled for M5.

Report anything unexpected with: check number, FL Studio version, sample rate, buffer size, what happened.
