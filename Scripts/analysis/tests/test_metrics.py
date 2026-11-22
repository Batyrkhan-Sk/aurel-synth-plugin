"""Validates the measurement code itself against signals with known answers.

If these fail, no measurement of AUREL's output can be trusted.
"""

import numpy as np
import pytest

from aurel_analysis import metrics

SR = 44100
N = SR * 2  # 2 s


def tone(freq, amp=1.0, phase=0.0, n=N, sr=SR):
    t = np.arange(n) / sr
    return amp * np.sin(2 * np.pi * freq * t + phase)


@pytest.mark.parametrize("freq", [41.2, 440.0, 997.0, 5011.3, 17999.9])
def test_frequency_estimate_is_accurate(freq):
    # Non-coherent frequencies (not a whole number of cycles) on purpose.
    est = metrics.estimate_frequency(tone(freq, 0.5, 0.3), SR)
    assert abs(est - freq) < 1e-3  # 0.001 Hz; < 0.05 cent even at 41 Hz


def test_amplitude_estimate():
    r = metrics.distortion(tone(997.0, 0.25), SR)
    assert r.fundamental_amplitude == pytest.approx(0.25, rel=1e-6)


def test_pure_float64_sine_has_negligible_thd():
    r = metrics.distortion(tone(997.0, 0.5), SR)
    assert r.thd_db < -180.0
    assert r.thd_n_db < -180.0


def test_known_harmonics_are_measured_exactly():
    # 2nd harmonic at -60 dB, 3rd at -80 dB relative to the fundamental.
    f0 = 997.0
    x = tone(f0) + tone(2 * f0, 1e-3, 1.0) + tone(3 * f0, 1e-4, 2.0)
    r = metrics.distortion(x, SR)
    assert r.harmonics_db[0] == pytest.approx(-60.0, abs=0.01)
    assert r.harmonics_db[1] == pytest.approx(-80.0, abs=0.01)
    expected = 10 * np.log10(1e-6 + 1e-8)
    assert r.thd_db == pytest.approx(expected, abs=0.01)


def test_saturation_is_detected():
    # Negative control: a driven tanh must register as heavy distortion.
    x = np.tanh(3.0 * tone(220.0))
    r = metrics.distortion(x, SR)
    assert r.thd_db > -20.0


def test_thd_n_tracks_added_noise():
    rng = np.random.default_rng(1234)
    sig = tone(997.0, 0.5)
    noise_rms = 0.5 / np.sqrt(2) * 10 ** (-100 / 20)  # -100 dB re signal RMS
    r = metrics.distortion(sig + rng.normal(0.0, noise_rms, N), SR)
    assert r.thd_n_db == pytest.approx(-100.0, abs=0.5)


def test_time_domain_helpers():
    x = np.array([0.5, -1.0, 0.5, 0.0])
    assert metrics.peak(x) == 1.0
    assert metrics.dc_offset(x) == 0.0
    assert metrics.rms(np.ones(10) * 0.5) == pytest.approx(0.5)
