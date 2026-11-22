"""M3 filter measurements: ladder solver comparison, self-oscillation tuning,
THD vs drive, aliasing of the driven ladder (with / without the 20 kHz input
band limit).

Production configuration: Newton, 2 iterations, tuning correction on, input
band-limited to 20 kHz (4th-order Butterworth).

Usage:
  .venv/Scripts/python.exe Scripts/analysis/m3_filter_measurements.py \
      --render build/vs2022-core/Tools/Render/Release/aurel_render.exe --out reports/m3
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from aurel_analysis import metrics  # noqa: E402
from aurel_analysis.wav import read_wav  # noqa: E402

RATE = 96000
BAND_LIMIT = 20000


def render(exe: Path, out: Path, seconds=1.0, **args) -> np.ndarray:
    cmd = [str(exe), "--signal", "filter", "--out", str(out), "--seconds", str(seconds), "--channels", "1",
           "--rate", str(args.pop("rate", RATE))]
    for k, v in args.items():
        cmd += [f"--{k}", str(v)]
    subprocess.run(cmd, check=True, capture_output=True)
    x, _ = read_wav(out)
    return x[:, 0]


def db(x: float) -> float:
    return 10 * np.log10(max(x, 1e-300))


def rel_diff_db(x: np.ndarray, ref: np.ndarray) -> float:
    return db(np.mean((x - ref) ** 2) / np.mean(ref ** 2))


def max_harmonic_delta(x: np.ndarray, ref: np.ndarray, f0: float) -> float:
    sx, sr = metrics.spectrum(x, RATE), metrics.spectrum(ref, RATE)
    p1 = sr.band_power(f0)
    delta = 0.0
    for h in range(1, int(20000 // f0)):
        pr = sr.band_power(f0 * h)
        if pr / p1 >= 1e-6:
            delta = max(delta, abs(db(sx.band_power(f0 * h)) - db(pr)))
    return delta


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    L = ["# M3 filter measurements", ""]
    steady = slice(RATE // 4, None)

    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "f.wav"

        # 1. Solver accuracy against the converged solution of the same equations.
        L += ["## 1. Solver accuracy vs converged Newton (ladder LP24, 110 Hz saw, full scale)", "",
              "Difference: power of (solver - converged) relative to the converged output. "
              "Δharm: largest level error of any harmonic above -60 dBc.", "",
              "| cutoff | k | drive | linearised: diff / Δharm | Newton-1 | Newton-2 (production) |",
              "|---|---|---|---|---|---|"]
        for fc in (300, 2000, 8000):
            for k in (0.0, 3.0, 3.9):
                for drive in (0, 12, 24):
                    common = dict(filter="ladder", cutoff=fc, feedback=k, drive=drive, input="saw", freq=110.0, amp=1.0)
                    ref = render(a.render, wav, solver="newton", iterations=32, **common)[steady]
                    cells = []
                    for solver, it in (("linear", 1), ("newton", 1), ("newton", 2)):
                        x = render(a.render, wav, solver=solver, iterations=it, **common)[steady]
                        cells.append(f"{rel_diff_db(x, ref):.0f} dB / {max_harmonic_delta(x, ref, 110.0):.2f} dB")
                    L.append(f"| {fc} | {k} | +{drive} dB | " + " | ".join(cells) + " |")
        L.append("")

        # 2. Self-oscillation tuning (production) vs the raw model.
        L += ["## 2. Self-oscillation pitch error, cents (k = 4.3, Newton-2, 4 s, last 2 s analysed)", "",
              "| cutoff (Hz) | raw model | with tuning correction | amplitude |", "|---|---|---|---|"]
        for fc in (110, 220, 440, 880, 1760, 3520, 7040, 14080):
            cells = []
            for tuning in (0, 1):
                x = render(a.render, wav, seconds=4.0, filter="ladder", solver="newton", tuning=tuning, cutoff=fc,
                           feedback=4.3, drive=0, input="impulse", amp=0.01)
                tail = x[2 * RATE:]
                cells.append(f"{1200 * np.log2(metrics.estimate_frequency(tail, RATE) / fc):+.2f}")
            cells.append(f"{np.sqrt(2 * np.mean(tail ** 2)):.3f}")
            L.append(f"| {fc} | " + " | ".join(cells) + " |")
        L.append("")

        # 3. THD vs drive.
        L += ["## 3. THD vs drive (ladder LP24, k = 0, Newton-2)", "",
              "| drive | sine 220 Hz, cutoff 2 kHz | sine 1 kHz, cutoff 1 kHz |", "|---|---|---|"]
        for drive in (-12, -6, 0, 6, 12, 18, 24):
            row = []
            for fsig, fc in ((220.0, 2000), (1000.0, 1000)):
                x = render(a.render, wav, filter="ladder", solver="newton", cutoff=fc, feedback=0, drive=drive,
                           input="sine", freq=fsig, amp=1.0)
                row.append(f"{metrics.distortion(x[steady], RATE, fsig).thd_db:.1f} dB")
            L.append(f"| {drive:+d} dB | " + " | ".join(row) + " |")
        L.append("")

        # 4. Aliasing.
        L += ["## 4. Ladder aliasing (saw input, internal 96 kHz), worst in-band alias, dBc", "",
              "| input | cutoff | k | drive | linearised | Newton-2 | Newton-2 + 20 kHz band limit (production) |",
              "|---|---|---|---|---|---|---|"]
        for f0, fc, k, drive in ((440.0, 5000, 3.5, 0), (1760.0, 10000, 3.5, 0), (3520.0, 20000, 0.0, 0),
                                 (880.0, 3000, 4.2, 0), (440.0, 5000, 3.5, 12), (1760.0, 10000, 3.5, 12),
                                 (440.0, 5000, 3.5, 24), (1760.0, 10000, 3.5, 24)):
            cells = []
            for solver, pre in (("linear", 0), ("newton", 0), ("newton", BAND_LIMIT)):
                x = render(a.render, wav, filter="ladder", solver=solver, cutoff=fc, feedback=k, drive=drive,
                           input="saw", freq=f0, amp=1.0, prefilter=pre)
                cells.append(f"{metrics.alias_analysis(x[steady], RATE, f0, 'saw').alias_worst_dbc:.1f}")
            L.append(f"| saw {f0:g} Hz | {fc} | {k} | {drive:+d} dB | " + " | ".join(cells) + " |")
        L.append("")

    text = "\n".join(L)
    (a.out / "summary.md").write_text(text, encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
