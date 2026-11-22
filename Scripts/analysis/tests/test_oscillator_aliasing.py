"""Regression guard for the M2a anti-aliasing decision (docs/dsp/oscillator-antialiasing.md).

The production kernel (sinc6) measured <= -110 dBc worst-case in-band alias
at AUREL's internal rates. The guard sits at -100 dBc: a 10 dB margin that
still catches any real regression (the next-best option, polyblep2, is at
-42..-53 dBc).
"""

import subprocess
from pathlib import Path

import pytest

from aurel_analysis import metrics
from aurel_analysis.wav import read_wav
from tests.test_render_pipeline import RENDER

pytestmark = pytest.mark.skipif(RENDER is None, reason="aurel_render not built")

PRODUCTION_KERNEL = "sinc6"
GUARD_DBC = -100.0


def render_osc(tmp_path: Path, kernel, wave, pw, f0, rate):
    out = tmp_path / "osc.wav"
    subprocess.run([str(RENDER), "--signal", "osc", "--kernel", kernel, "--wave", wave,
                    "--pw", str(pw), "--freq", str(f0), "--rate", str(rate),
                    "--seconds", "1", "--channels", "1", "--out", str(out)],
                   check=True, capture_output=True)
    x, sr = read_wav(out)
    return x[:, 0], sr


@pytest.mark.parametrize("rate", [88200, 96000])
@pytest.mark.parametrize("wave,pw", [("saw", 0.5), ("pulse", 0.1), ("triangle", 0.5)])
@pytest.mark.parametrize("f0", [440.0, 3520.0, 4186.009])
def test_production_kernel_alias_below_guard(tmp_path, rate, wave, pw, f0):
    x, sr = render_osc(tmp_path, PRODUCTION_KERNEL, wave, pw, f0, rate)
    r = metrics.alias_analysis(x, sr, f0, wave, pw)
    assert r.alias_worst_dbc < GUARD_DBC
    assert r.harmonic_error_20k_db < 0.1


def test_harness_detects_naive_aliasing(tmp_path):
    # Sanity: if this fails, the measurement (not the oscillator) is broken.
    x, sr = render_osc(tmp_path, "naive", "saw", 0.5, 3520.0, 96000)
    assert metrics.alias_analysis(x, sr, 3520.0, "saw").alias_worst_dbc > -40.0
