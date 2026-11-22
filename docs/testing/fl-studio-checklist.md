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

Report anything unexpected with: check number, FL Studio version, sample rate, buffer size, what happened.
