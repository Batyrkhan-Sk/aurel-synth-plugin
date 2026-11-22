"""Regression guards for the filter in the voice (M3b, docs/dsp/filters-m3.md).

End to end: oscillator -> 2x voice -> band-limited ladder / SVF -> decimator
-> host rate. Bass compensation is applied at the ladder output; at the
input it cost 15-40 dB of alias rejection (measured in M3b).
"""

import subprocess
from pathlib import Path

import pytest

from aurel_analysis import metrics
from aurel_analysis.wav import read_wav
from tests.test_render_pipeline import RENDER

pytestmark = pytest.mark.skipif(RENDER is None, reason="aurel_render not built")


def voice(tmp_path: Path, rate, note, filt, cutoff, res, drive=0.0):
    out = tmp_path / "v.wav"
    subprocess.run([str(RENDER), "--signal", "note", "--note", str(note), "--amp", "1", "--rate", str(rate),
                    "--seconds", "1", "--channels", "1", "--wave", "saw", "--voice-filter", filt,
                    "--cutoff", str(cutoff), "--res", str(res), "--drive", str(drive), "--comp", "0.5",
                    "--out", str(out)], check=True, capture_output=True)
    x, sr = read_wav(out)
    f0 = 440.0 * 2 ** ((note - 69) / 12)
    return metrics.alias_analysis(x[int(0.1 * sr):, 0], sr, f0, "saw").alias_worst_dbc


@pytest.mark.parametrize("rate,note,filt,cutoff,res,drive,guard", [
    (44100, 69, "ladder24", 5000, 0.6, 0, -100.0),   # measured -106.5
    (48000, 81, "ladder24", 3000, 0.85, 0, -100.0),  # measured -109.5
    (44100, 57, "ladder12", 2000, 0.9, 0, -85.0),    # measured -92.3
    (48000, 69, "ladder24", 5000, 0.6, 12, -72.0),   # measured -78.4
    (48000, 93, "svf", 8000, 0.6, 0, -115.0),        # measured -125.9
])
def test_voice_filter_alias(tmp_path, rate, note, filt, cutoff, res, drive, guard):
    assert voice(tmp_path, rate, note, filt, cutoff, res, drive) < guard
