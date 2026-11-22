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


def additive_saw(f0, sr=96000, n=96000):
    """Ideal band-limited saw (+/-1 naive shape), float64, harmonics below Nyquist."""
    t = np.arange(n) / sr
    x = np.zeros(n)
    for k in range(1, int((sr / 2) // f0) + 1):
        # 2*phase - 1 saw = -(2/pi) * sum sin(2 pi k f t) / k
        x -= (2 / (np.pi * k)) * np.sin(2 * np.pi * k * f0 * t)
    return x


def test_alias_metric_on_ideal_saw_finds_no_alias_and_exact_harmonics():
    f0 = 1760.0
    r = metrics.alias_analysis(additive_saw(f0), 96000, f0, "saw")
    assert r.alias_worst_dbc < -180
    assert r.harmonic_error_20k_db < 1e-3


def test_alias_metric_measures_planted_alias():
    f0 = 1760.0
    x = additive_saw(f0)
    fund_amp = 2 / np.pi
    alias_amp = fund_amp * 10 ** (-70 / 20)
    t = np.arange(len(x)) / 96000
    x = x + alias_amp * np.sin(2 * np.pi * 7123.0 * t)  # between harmonics 4 and 5
    r = metrics.alias_analysis(x, 96000, f0, "saw")
    assert r.alias_worst_dbc == pytest.approx(-70.0, abs=0.05)
    assert r.alias_total_dbc == pytest.approx(-70.0, abs=0.05)


def test_alias_metric_ignores_content_above_audible_band():
    # A component at 30 kHz is removed by AUREL's decimator: not counted.
    f0 = 1760.0
    x = additive_saw(f0)
    t = np.arange(len(x)) / 96000
    x = x + 0.1 * np.sin(2 * np.pi * 30001.0 * t)
    assert metrics.alias_analysis(x, 96000, f0, "saw").alias_worst_dbc < -180


def test_time_domain_helpers():
    x = np.array([0.5, -1.0, 0.5, 0.0])
    assert metrics.peak(x) == 1.0
    assert metrics.dc_offset(x) == 0.0
    assert metrics.rms(np.ones(10) * 0.5) == pytest.approx(0.5)
