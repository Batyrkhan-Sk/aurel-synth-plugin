"""M2b measurements: end-to-end engine aliasing (oversampled voice + decimator),
hard sync, and through-zero FM.

Usage:
  .venv/Scripts/python.exe Scripts/analysis/m2b_measurements.py \
      --render build/vs2022-core/Tools/Render/Release/aurel_render.exe --out reports/m2b
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from aurel_analysis import metrics  # noqa: E402
from aurel_analysis.wav import read_wav  # noqa: E402

NOTES = {"A2": 45, "A4": 69, "A6": 93, "A7": 105, "C8": 108}


def run(exe: Path, out: Path, **args):
    cmd = [str(exe), "--out", str(out), "--seconds", "1", "--channels", "1"]
    for k, v in args.items():
        cmd += [f"--{k.replace('_', '-')}", str(v)]
    subprocess.run(cmd, check=True, capture_output=True)
    x, sr = read_wav(out)
    return x[:, 0], sr


def note_hz(n: int) -> float:
    return 440.0 * 2 ** ((n - 69) / 12)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    lines = ["# M2b measurements", ""]

    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "x.wav"

        lines += ["## End-to-end engine (2x voice + half-band decimator), worst in-band alias, dBc", "",
                  "| host rate | wave | " + " | ".join(NOTES) + " |", "|---|---|" + "---|" * len(NOTES)]
        for rate in (44100, 48000, 96000):
            for wave in ("saw", "pulse", "triangle"):
                cells = []
                for n in NOTES.values():
                    x, sr = run(a.render, wav, signal="note", note=n, amp=1.0, rate=rate, wave=wave)
                    steady = x[int(0.05 * sr):]
                    cells.append(f"{metrics.alias_analysis(steady, sr, note_hz(n), wave).alias_worst_dbc:.1f}")
                lines.append(f"| {rate} | {wave} | " + " | ".join(cells) + " |")
        lines.append("")

        lines += ["## Hard sync (raw oscillator, 96 kHz), worst in-band alias, dBc", "",
                  "| slave wave | ratio | 220 Hz | 880 Hz | 1760 Hz | 3520 Hz |", "|---|---|---|---|---|---|"]
        for wave in ("saw", "pulse", "triangle", "sine"):
            for ratio in (1.5, 2.37, 4.9):
                cells = []
                for f0 in (220.0, 880.0, 1760.0, 3520.0):
                    x, sr = run(a.render, wav, signal="osc", wave=wave, freq=f0, rate=96000, sync=ratio)
                    # The synced waveform is periodic at the master frequency f0;
                    # "harmonic" bins are k*f0, everything else is alias.
                    cells.append(f"{metrics.alias_analysis(x, sr, f0, 'saw').alias_worst_dbc:.1f}")
                lines.append(f"| {wave} | {ratio} | " + " | ".join(cells) + " |")
        lines.append("")

        lines += ["## Through-zero linear FM (raw oscillator, 96 kHz, sine modulator at carrier frequency), "
                  "worst in-band alias, dBc", "",
                  "| carrier wave | depth | 220 Hz | 880 Hz | 1760 Hz |", "|---|---|---|---|---|"]
        for wave in ("sine", "triangle", "saw", "pulse"):
            for depth in (0.5, 1.5, 4.0):
                cells = []
                for f0 in (220.0, 880.0, 1760.0):
                    x, sr = run(a.render, wav, signal="osc", wave=wave, freq=f0, rate=96000, fm=depth)
                    cells.append(f"{metrics.alias_analysis(x, sr, f0, 'saw').alias_worst_dbc:.1f}")
                lines.append(f"| {wave} | {depth} | " + " | ".join(cells) + " |")
        lines.append("")

    text = "\n".join(lines)
    (a.out / "summary.md").write_text(text, encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
