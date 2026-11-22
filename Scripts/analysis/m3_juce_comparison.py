"""Compares AUREL's ladder with juce::dsp::LadderFilter under identical conditions.

Inputs: WAVs from aurel_juce_baseline (--juce-dir) and renders from
aurel_render. Metrics:
  * cutoff accuracy: the frequency where the small-signal LP24 response
    (k = 0) crosses -12.04 dB (the analog ladder's level at its cutoff),
    expressed as cents from the requested cutoff;
  * response error: max |dB difference| to the analog ladder 1/(1 + jf/fc)^4
    up to 20 kHz (and up to the cutoff);
  * self-oscillation at maximum resonance: frequency error and amplitude.

Usage:
  .venv/Scripts/python.exe Scripts/analysis/m3_juce_comparison.py \
      --render build/vs2022-core/Tools/Render/Release/aurel_render.exe \
      --juce-dir reports/m3/juce
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
CUTOFFS = (100, 1000, 5000, 10000, 15000, 20000)
TARGET_DB = 20 * np.log10(0.5 ** 2)  # (1/sqrt(2))^4 = -12.04 dB


def response_db(ir: np.ndarray, impulse: float, freqs: np.ndarray) -> np.ndarray:
    n = len(ir)
    H = np.array([np.sum(ir * np.exp(-2j * np.pi * f * np.arange(n) / RATE)) for f in freqs]) / impulse
    return 20 * np.log10(np.abs(H))


def cutoff_error_cents(ir: np.ndarray, impulse: float, fc: float) -> float:
    freqs = fc * 2 ** (np.linspace(-1, 1, 801))  # +/- one octave, 0.25-cent grid
    freqs = freqs[freqs < 0.49 * RATE]
    r = response_db(ir, impulse, freqs)
    idx = np.where(np.diff(np.sign(r - TARGET_DB)))[0]
    if len(idx) == 0:
        return float("nan")
    i = idx[0]
    f = freqs[i] + (TARGET_DB - r[i]) * (freqs[i + 1] - freqs[i]) / (r[i + 1] - r[i])
    return 1200 * np.log2(f / fc)


def max_error_db(ir: np.ndarray, impulse: float, fc: float, fmax: float) -> float:
    """Only where the analog response is above -60 dB: deeper stopband levels
    are below the float precision of the rendered impulse response."""
    freqs = np.geomspace(20, fmax, 200)
    analog = -80 * np.log10(np.abs(1 + 1j * freqs / fc))  # 20*log10(|1 + jf/fc|^-4)
    keep = analog > -60.0
    return float(np.max(np.abs(response_db(ir, impulse, freqs[keep]) - analog[keep])))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True, type=Path)
    ap.add_argument("--juce-dir", required=True, type=Path)
    a = ap.parse_args()

    L = ["## JUCE LadderFilter vs AUREL ladder (96 kHz, LP24)", "",
         "| cutoff | cutoff error, cents (JUCE / AUREL) | max response error vs analog ladder (to 20 kHz, above -60 dB), dB (JUCE / AUREL) "
         "| self-osc freq error, cents (JUCE / AUREL) | self-osc amplitude (JUCE / AUREL) |",
         "|---|---|---|---|---|"]
    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "a.wav"

        def aurel(**args) -> np.ndarray:
            cmd = [str(a.render), "--signal", "filter", "--out", str(wav), "--channels", "1", "--rate", str(RATE),
                   "--filter", "ladder", "--solver", "newton"]
            for k, v in args.items():
                cmd += [f"--{k}", str(v)]
            subprocess.run(cmd, check=True, capture_output=True)
            return read_wav(wav)[0][:, 0]

        for fc in CUTOFFS:
            j_ir = read_wav(a.juce_dir / f"juce_ladder_ir_{fc}.wav")[0][:, 0]
            a_ir = aurel(seconds=1, cutoff=fc, feedback=0, drive=0, input="impulse", amp=1e-4)
            ce = [cutoff_error_cents(x, 1e-4, fc) for x in (j_ir, a_ir)]
            me = [max_error_db(x, 1e-4, fc, 20000.0) for x in (j_ir, a_ir)]

            j_osc = read_wav(a.juce_dir / f"juce_ladder_selfosc_{fc}.wav")[0][:, 0]
            # AUREL at a resonance just past the threshold (k = 4.3; the
            # parameter mapping to the user control is decided in M3b).
            a_osc = aurel(seconds=4, cutoff=fc, feedback=4.3, drive=0, input="impulse", amp=0.01)
            osc = []
            for x in (j_osc, a_osc):
                tail = x[2 * RATE:]
                amp = float(np.sqrt(2 * np.mean(tail ** 2)))
                if amp < 1e-4:
                    osc.append(("no oscillation", f"{amp:.1e}"))
                else:
                    osc.append((f"{1200 * np.log2(metrics.estimate_frequency(tail, RATE) / fc):+.1f}", f"{amp:.3f}"))
            L.append(f"| {fc} | {ce[0]:+.1f} / {ce[1]:+.2f} | {me[0]:.2f} / {me[1]:.2f} | "
                     f"{osc[0][0]} / {osc[1][0]} | {osc[0][1]} / {osc[1][1]} |")

    print("\n".join(L))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
