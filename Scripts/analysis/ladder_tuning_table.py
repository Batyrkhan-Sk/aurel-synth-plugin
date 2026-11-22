"""Measures the ladder's uncorrected self-oscillation pitch offset vs feedback k
and prints the table for LadderFilter::kSelfOscillationOffsetCents.

The offset is a property of the nonlinear model (the tanh terms set the
oscillation amplitude, which pulls the pitch flat); it depends on k, not on
the cutoff (verified in docs/dsp/filters-m3.md).

Usage:
  .venv/Scripts/python.exe Scripts/analysis/ladder_tuning_table.py \
      --render build/vs2022-core/Tools/Render/Release/aurel_render.exe
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
CUTOFF = 1000.0


def offset_cents(exe: Path, wav: Path, k: float) -> float:
    subprocess.run([str(exe), "--signal", "filter", "--out", str(wav), "--seconds", "6", "--channels", "1",
                    "--rate", str(RATE), "--filter", "ladder", "--solver", "newton", "--iterations", "2",
                    "--tuning", "0", "--cutoff", str(CUTOFF), "--feedback", str(k), "--drive", "0",
                    "--input", "impulse", "--amp", "0.01"], check=True, capture_output=True)
    x, _ = read_wav(wav)
    tail = x[4 * RATE:, 0]
    return 1200.0 * np.log2(metrics.estimate_frequency(tail, RATE) / CUTOFF)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True, type=Path)
    a = ap.parse_args()
    values = [0.0]  # k = 4.0: marginally stable, linear -> exactly at the cutoff
    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "t.wav"
        for i in range(1, 21):
            values.append(offset_cents(a.render, wav, 4.0 + i / 10.0))
    rows = [", ".join(f"{v:.3f}" for v in values[j:j + 7]) for j in range(0, 21, 7)]
    print("static constexpr std::array<double, 21> kSelfOscillationOffsetCents {")
    print("    " + ",\n    ".join(rows))
    print("};")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
