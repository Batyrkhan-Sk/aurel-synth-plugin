"""End-to-end check of the C++ render -> WAV -> Python measurement path.

aurel_render's reference sine is computed in double precision and stored as
float32, so the only expected error is float32 rounding (~-150 dB). Any
larger error points at the renderer, the WAV writer or the reader.
"""

import json
import os
import subprocess
from pathlib import Path

import numpy as np
import pytest

from aurel_analysis import metrics
from aurel_analysis.wav import read_wav

REPO = Path(__file__).resolve().parents[3]


def find_render_exe() -> Path | None:
    env = os.environ.get("AUREL_RENDER")
    if env:
        return Path(env)
    candidates = sorted(
        REPO.glob("build/*/Tools/Render/*/aurel_render.exe"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    return candidates[0] if candidates else None


RENDER = find_render_exe()
pytestmark = pytest.mark.skipif(RENDER is None, reason="aurel_render not built")


def render(tmp_path, **args) -> tuple[np.ndarray, int]:
    out = tmp_path / "render.wav"
    cmd = [str(RENDER), "--out", str(out)]
    for k, v in args.items():
        cmd += [f"--{k}", str(v)]
    result = subprocess.run(cmd, capture_output=True, text=True, check=True)
    info = json.loads(result.stdout)
    assert info["frames"] > 0
    return read_wav(out)


@pytest.mark.parametrize("rate", [44100, 48000, 88200, 96000, 176400, 192000])
@pytest.mark.parametrize("freq", [997.0, 1000.0])
def test_reference_sine_measures_correctly(tmp_path, rate, freq):
    x, sr = render(tmp_path, signal="sine", freq=freq, amp=0.5, rate=rate,
                   seconds=1, channels=2)
    assert sr == rate
    assert x.shape == (rate, 2)
    assert np.array_equal(x[:, 0], x[:, 1])

    r = metrics.distortion(x[:, 0], sr)
    assert abs(r.fundamental_hz - freq) < 1e-3
    assert r.fundamental_amplitude == pytest.approx(0.5, rel=1e-6)
    assert r.thd_n_db < -130.0
    assert abs(metrics.dc_offset(x[:, 0])) < 1e-6


@pytest.mark.parametrize("rate", [44100, 48000, 96000, 192000])
@pytest.mark.parametrize("note", [45, 69, 93])
def test_engine_note_pitch_level_and_purity(tmp_path, rate, note):
    # M1 voice: sine with a 2 ms attack. Analyse after the attack so the
    # envelope's amplitude modulation is excluded.
    x, sr = render(tmp_path, signal="note", note=note, amp=1.0, rate=rate,
                   seconds=1, channels=2)
    assert np.array_equal(x[:, 0], x[:, 1])
    steady = x[int(0.05 * sr):, 0]

    expected_hz = 440.0 * 2 ** ((note - 69) / 12)
    r = metrics.distortion(steady, sr)
    cents = 1200 * np.log2(r.fundamental_hz / expected_hz)
    assert abs(cents) < 0.001
    assert r.fundamental_amplitude == pytest.approx(0.25, rel=1e-5)  # Voice::kVelocityGain
    assert r.thd_n_db < -130.0


def test_silence_is_exactly_zero(tmp_path):
    x, _ = render(tmp_path, signal="silence", rate=48000, seconds=0.5, channels=1)
    assert x.shape == (24000, 1)
    assert metrics.peak(x) == 0.0


def test_invalid_arguments_are_rejected(tmp_path):
    out = tmp_path / "x.wav"
    for bad in (["--freq", "30000", "--rate", "48000"],  # above Nyquist
                ["--signal", "square"],
                ["--channels", "3"]):
        r = subprocess.run([str(RENDER), "--out", str(out), *bad], capture_output=True)
        assert r.returncode == 1
    assert not out.exists()
