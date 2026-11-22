"""M2a: anti-aliasing kernel comparison for the AUREL oscillator.

Renders every kernel x waveform x note x sample rate with aurel_render,
measures in-band aliasing and harmonic accuracy, and writes:
  <out>/aliasing.csv     all measurements
  <out>/summary.md       per-rate tables (worst case over notes)
  <out>/aliasing_saw.png worst alias vs fundamental, saw

Usage:
  .venv/Scripts/python.exe Scripts/analysis/oscillator_aliasing.py \
      --render build/vs2022/Tools/Render/Release/aurel_render.exe --out reports/m2a
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import matplotlib  # noqa: E402

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

from aurel_analysis import metrics  # noqa: E402
from aurel_analysis.wav import read_wav  # noqa: E402

KERNELS = ["naive", "polyblep2", "sinc4", "sinc6", "sinc8", "sinc12"]
WAVES = [("saw", 0.5), ("pulse", 0.5), ("pulse", 0.1), ("triangle", 0.5)]
NOTES = {"A2": 110.0, "A4": 440.0, "A5": 880.0, "A6": 1760.0, "A7": 3520.0, "C8": 4186.009}
RATES = [48000, 88200, 96000, 192000]  # 48k = 1x host rate; 88.2/96k = AUREL internal 2x


def render(exe: Path, out: Path, kernel: str, wave: str, pw: float, f0: float, rate: int):
    subprocess.run(
        [str(exe), "--signal", "osc", "--kernel", kernel, "--wave", wave, "--pw", str(pw),
         "--freq", str(f0), "--rate", str(rate), "--seconds", "1", "--channels", "1",
         "--out", str(out)],
        check=True, capture_output=True)
    x, sr = read_wav(out)
    return x[:, 0], sr


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "osc.wav"
        for rate in RATES:
            for kernel in KERNELS:
                for wave, pw in WAVES:
                    for note, f0 in NOTES.items():
                        x, sr = render(args.render, wav, kernel, wave, pw, f0, rate)
                        r = metrics.alias_analysis(x, sr, f0, wave, pw)
                        rows.append(dict(rate=rate, kernel=kernel, wave=wave, pw=pw, note=note, f0=f0,
                                         alias_total_dbc=r.alias_total_dbc,
                                         alias_worst_dbc=r.alias_worst_dbc,
                                         harm_err_10k_db=r.harmonic_error_10k_db,
                                         harm_err_20k_db=r.harmonic_error_20k_db))
            print(f"rate {rate} done", flush=True)

    with open(args.out / "aliasing.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    lines = ["# M2a oscillator aliasing summary", "",
             "Worst single in-band (20 Hz-20 kHz) alias component, dBc, worst case over notes "
             "A2..C8. Lower is better. Harmonic error = max |level error| of harmonics <= 20 kHz.", ""]
    for rate in RATES:
        lines += [f"## {rate} Hz", "",
                  "| kernel | " + " | ".join(f"{w} pw={pw}" for w, pw in WAVES) + " | harm. err <=20k (dB) |",
                  "|---|" + "---|" * (len(WAVES) + 1)]
        for kernel in KERNELS:
            cells = []
            herr = 0.0
            for wave, pw in WAVES:
                sel = [r for r in rows if r["rate"] == rate and r["kernel"] == kernel
                       and r["wave"] == wave and r["pw"] == pw]
                cells.append(f"{max(r['alias_worst_dbc'] for r in sel):.1f}")
                herr = max(herr, max(r["harm_err_20k_db"] for r in sel))
            lines.append(f"| {kernel} | " + " | ".join(cells) + f" | {herr:.2f} |")
        lines.append("")
    (args.out / "summary.md").write_text("\n".join(lines), encoding="utf-8")

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.5), sharey=True)
    for ax, rate in zip(axes, (48000, 96000)):
        for kernel in KERNELS:
            sel = [r for r in rows if r["rate"] == rate and r["kernel"] == kernel and r["wave"] == "saw"]
            ax.semilogx([r["f0"] for r in sel], [r["alias_worst_dbc"] for r in sel], marker="o", label=kernel)
        ax.axhline(-80, color="k", ls="--", lw=0.8, label="target -80 dBc")
        ax.set_title(f"Saw, {rate} Hz: worst in-band alias")
        ax.set_xlabel("fundamental (Hz)")
        ax.grid(True, which="both", alpha=0.3)
    axes[0].set_ylabel("dBc")
    axes[1].legend(loc="lower right", fontsize=8)
    fig.tight_layout()
    fig.savefig(args.out / "aliasing_saw.png", dpi=120)

    print((args.out / "summary.md").read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
