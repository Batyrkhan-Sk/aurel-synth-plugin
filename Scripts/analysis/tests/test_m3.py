"""Regression guards for the M3 filter decisions (docs/dsp/filters-m3.md).

Production ladder: Newton, 2 iterations, self-oscillation tuning on,
input band-limited to 20 kHz. Guards sit a few dB / cents outside the
measured values.
"""

import subprocess
from pathlib import Path

import numpy as np
import pytest

from aurel_analysis import metrics
from aurel_analysis.wav import read_wav
from tests.test_render_pipeline import RENDER

pytestmark = pytest.mark.skipif(RENDER is None, reason="aurel_render not built")
RATE = 96000


def ladder(tmp_path: Path, seconds=1.0, **args) -> np.ndarray:
    out = tmp_path / "f.wav"
    cmd = [str(RENDER), "--signal", "filter", "--filter", "ladder", "--out", str(out), "--seconds", str(seconds),
           "--channels", "1", "--rate", str(RATE)]
    for k, v in args.items():
        cmd += [f"--{k}", str(v)]
    subprocess.run(cmd, check=True, capture_output=True)
    return read_wav(out)[0][:, 0]


@pytest.mark.parametrize("fc", [220.0, 880.0, 3520.0])
def test_self_oscillation_is_in_tune(tmp_path, fc):
    x = ladder(tmp_path, seconds=4.0, solver="newton", cutoff=fc, feedback=4.3, drive=0, input="impulse", amp=0.01)
    tail = x[2 * RATE:]
    cents = 1200 * np.log2(metrics.estimate_frequency(tail, RATE) / fc)
    assert abs(cents) < 1.0  # measured <= 0.23


@pytest.mark.parametrize("f0,fc,k", [(440.0, 5000, 3.5), (1760.0, 10000, 3.5), (880.0, 3000, 4.2)])
def test_band_limited_ladder_alias_at_default_drive(tmp_path, f0, fc, k):
    x = ladder(tmp_path, solver="newton", cutoff=fc, feedback=k, drive=0, input="saw", freq=f0, amp=1.0,
               prefilter=20000)
    assert metrics.alias_analysis(x[RATE // 4:], RATE, f0, "saw").alias_worst_dbc < -90.0  # measured <= -97.9


def test_two_newton_iterations_match_the_converged_solution(tmp_path):
    common = dict(solver="newton", cutoff=8000, feedback=3.9, drive=24, input="saw", freq=110.0, amp=1.0)
    ref = ladder(tmp_path, iterations=32, **common)[RATE // 4:]
    x = ladder(tmp_path, iterations=2, **common)[RATE // 4:]
    diff_db = 10 * np.log10(np.mean((x - ref) ** 2) / np.mean(ref ** 2))
    assert diff_db < -70.0  # measured -77 (hardest case in the matrix)
