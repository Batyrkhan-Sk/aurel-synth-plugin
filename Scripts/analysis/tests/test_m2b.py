"""Regression guards for M2b (docs/dsp/oscillators-m2b.md).

Guards sit ~5-10 dB above the measured values so they catch regressions
without being flaky. Known limitations (sine-slave sync at extreme ratios,
FM of saw/pulse carriers at high notes) are documented and decided in M5;
their guards only stop them from getting *worse*.
"""

import subprocess
from pathlib import Path

import pytest

from aurel_analysis import metrics
from aurel_analysis.wav import read_wav
from tests.test_render_pipeline import RENDER

pytestmark = pytest.mark.skipif(RENDER is None, reason="aurel_render not built")


def run(tmp_path: Path, **args):
    out = tmp_path / "x.wav"
    cmd = [str(RENDER), "--out", str(out), "--seconds", "1", "--channels", "1"]
    for k, v in args.items():
        cmd += [f"--{k.replace('_', '-')}", str(v)]
    subprocess.run(cmd, check=True, capture_output=True)
    x, sr = read_wav(out)
    return x[:, 0], sr


@pytest.mark.parametrize("rate", [44100, 48000])
@pytest.mark.parametrize("wave", ["saw", "pulse", "triangle"])
@pytest.mark.parametrize("note", [69, 108])
def test_engine_end_to_end_alias(tmp_path, rate, wave, note):
    x, sr = run(tmp_path, signal="note", note=note, amp=1.0, rate=rate, wave=wave)
    f0 = 440.0 * 2 ** ((note - 69) / 12)
    r = metrics.alias_analysis(x[int(0.05 * sr):], sr, f0, wave)
    assert r.alias_worst_dbc < -110.0  # measured <= -116.9


@pytest.mark.parametrize("wave", ["saw", "pulse", "triangle"])
@pytest.mark.parametrize("f0", [880.0, 3520.0])
def test_sync_alias(tmp_path, wave, f0):
    x, sr = run(tmp_path, signal="osc", wave=wave, freq=f0, rate=96000, sync=2.37)
    assert metrics.alias_analysis(x, sr, f0, "saw").alias_worst_dbc < -100.0  # measured <= -106.6


def test_sync_sine_known_limit_does_not_regress(tmp_path):
    x, sr = run(tmp_path, signal="osc", wave="sine", freq=3520.0, rate=96000, sync=2.37)
    assert metrics.alias_analysis(x, sr, 3520.0, "saw").alias_worst_dbc < -60.0  # measured -66.8


@pytest.mark.parametrize("depth", [0.5, 1.5, 4.0])
def test_fm_sine_carrier_is_clean(tmp_path, depth):
    x, sr = run(tmp_path, signal="osc", wave="sine", freq=1760.0, rate=96000, fm=depth)
    assert metrics.alias_analysis(x, sr, 1760.0, "saw").alias_worst_dbc < -150.0  # measured <= -164.8


def test_fm_saw_known_limit_does_not_regress(tmp_path):
    x, sr = run(tmp_path, signal="osc", wave="saw", freq=880.0, rate=96000, fm=1.5)
    assert metrics.alias_analysis(x, sr, 880.0, "saw").alias_worst_dbc < -47.0  # measured -52.5
