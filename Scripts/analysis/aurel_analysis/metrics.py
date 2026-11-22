"""Spectral and time-domain metrics.

Method notes
------------
* Window: Kaiser, beta = 24. Its sidelobes sit around -220 dB, far below
  float32 quantisation (~-150 dB), so window leakage never masks the
  quantity being measured. The cost is a wide main lobe (about +/-8 bins),
  which is why harmonic bands are +/-12 bins wide.
* Component power is taken as the sum of |X|^2 over the component's band,
  normalised so it equals the component's mean-square value. For a sine of
  amplitude A this is A^2 / 2.
* All computations are float64. The signal is not assumed to contain a
  whole number of cycles.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy.signal import windows

KAISER_BETA = 24.0
BAND_HALF_WIDTH_BINS = 12


@dataclass(frozen=True)
class Spectrum:
    power: np.ndarray       # one-sided, normalised: sum over a band = mean-square
    bin_hz: float
    sample_rate: int

    def band_power(self, centre_hz: float, half_width_bins: int = BAND_HALF_WIDTH_BINS) -> float:
        centre = int(round(centre_hz / self.bin_hz))
        lo = max(0, centre - half_width_bins)
        hi = min(len(self.power), centre + half_width_bins + 1)
        return float(np.sum(self.power[lo:hi]))


def spectrum(x: np.ndarray, sample_rate: int, pad: int = 1) -> Spectrum:
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    w = windows.kaiser(n, KAISER_BETA, sym=False)
    nfft = n * pad
    X = np.fft.rfft(x * w, nfft)
    # Parseval for a one-sided spectrum; pad rescales bin density.
    power = (2.0 / (n * np.sum(w * w) * pad)) * np.abs(X) ** 2
    power[0] *= 0.5
    if nfft % 2 == 0:
        power[-1] *= 0.5
    return Spectrum(power=power, bin_hz=sample_rate / nfft, sample_rate=sample_rate)


def estimate_frequency(x: np.ndarray, sample_rate: int) -> float:
    """Frequency of the strongest component, in Hz.

    Peak bin of an 8x zero-padded Kaiser spectrum, refined by parabolic
    interpolation on log power. With the main lobe spread over ~130 padded
    bins the interpolation bias is negligible (verified in the tests).
    """
    spec = spectrum(x, sample_rate, pad=8)
    p = spec.power
    k = int(np.argmax(p[1:-1])) + 1
    a, b, c = np.log(p[k - 1 : k + 2] + 1e-300)
    denom = a - 2.0 * b + c
    delta = 0.0 if denom == 0.0 else 0.5 * (a - c) / denom
    return (k + delta) * spec.bin_hz


@dataclass(frozen=True)
class DistortionResult:
    fundamental_hz: float
    fundamental_amplitude: float  # peak amplitude of the fundamental
    thd_db: float                 # harmonics 2..N relative to fundamental power
    thd_n_db: float               # everything except DC and fundamental, relative
    harmonics_db: list[float]     # level of harmonic k (k = 2..) relative to fundamental


def distortion(x: np.ndarray, sample_rate: int, f0: float | None = None,
               max_harmonic: int = 20) -> DistortionResult:
    if f0 is None:
        f0 = estimate_frequency(x, sample_rate)
    spec = spectrum(x, sample_rate)
    nyquist = sample_rate / 2.0
    guard_hz = (BAND_HALF_WIDTH_BINS + 1) * spec.bin_hz

    p1 = spec.band_power(f0)
    harmonic_powers = []
    for k in range(2, max_harmonic + 1):
        fk = k * f0
        if fk > nyquist - guard_hz:
            break
        harmonic_powers.append(spec.band_power(fk))

    # Sum the residual bins directly rather than computing total - p1, which
    # would cancel catastrophically (float64 floor near -160 dB).
    mask = np.ones(len(spec.power), dtype=bool)
    mask[: BAND_HALF_WIDTH_BINS + 1] = False  # DC band
    c = int(round(f0 / spec.bin_hz))
    mask[max(0, c - BAND_HALF_WIDTH_BINS) : c + BAND_HALF_WIDTH_BINS + 1] = False
    noise_and_distortion = float(np.sum(spec.power[mask]))

    floor = 1e-30
    return DistortionResult(
        fundamental_hz=f0,
        fundamental_amplitude=float(np.sqrt(2.0 * p1)),
        thd_db=10.0 * np.log10(max(sum(harmonic_powers), floor) / p1),
        thd_n_db=10.0 * np.log10(max(noise_and_distortion, floor) / p1),
        harmonics_db=[10.0 * np.log10(max(h, floor) / p1) for h in harmonic_powers],
    )


def ideal_harmonic_amplitude(wave: str, k: int, pulse_width: float = 0.5) -> float:
    """Peak amplitude of harmonic k of the ideal (continuous-time) +/-1 waveform."""
    if wave == "saw":
        return 2.0 / (np.pi * k)
    if wave == "pulse":
        return 4.0 / (np.pi * k) * abs(np.sin(np.pi * k * pulse_width))
    if wave == "triangle":
        return 8.0 / (np.pi ** 2 * k ** 2) if k % 2 else 0.0
    if wave == "sine":
        return 1.0 if k == 1 else 0.0
    raise ValueError(wave)


@dataclass(frozen=True)
class AliasResult:
    f0: float
    alias_total_dbc: float    # all in-band non-harmonic power, re fundamental
    alias_worst_dbc: float    # strongest single alias component, re fundamental
    harmonic_error_10k_db: float  # max |level error| of harmonics <= 10 kHz vs ideal
    harmonic_error_20k_db: float  # same, <= 20 kHz


def alias_analysis(x: np.ndarray, sample_rate: int, f0: float, wave: str,
                   pulse_width: float = 0.5, audible_hz: float = 20000.0) -> AliasResult:
    """Aliasing that lands in the audible band (20 Hz .. audible_hz).

    Everything that is not within +/-BAND_HALF_WIDTH_BINS of a true harmonic
    (k*f0 below Nyquist) or DC counts as alias. Content between audible_hz
    and Nyquist is ignored: in AUREL it is removed by the decimator.
    """
    spec = spectrum(x, sample_rate)
    n = len(spec.power)
    freqs = np.arange(n) * spec.bin_hz
    hw = BAND_HALF_WIDTH_BINS

    harmonic = np.zeros(n, dtype=bool)
    for k in range(1, int((sample_rate / 2) // f0) + 1):
        c = int(round(k * f0 / spec.bin_hz))
        harmonic[max(0, c - hw): c + hw + 1] = True
    harmonic[: hw + 1] = True  # DC band

    alias_mask = (freqs >= 20.0) & (freqs <= audible_hz) & ~harmonic
    p1 = spec.band_power(f0)
    alias_power = float(np.sum(spec.power[alias_mask]))

    masked = np.where(alias_mask, spec.power, 0.0)
    local = np.convolve(masked, np.ones(2 * hw + 1), mode="same")
    worst = float(local.max()) if local.size else 0.0

    floor = 1e-30
    a1 = ideal_harmonic_amplitude(wave, 1, pulse_width)
    err10, err20 = 0.0, 0.0
    for k in range(1, int(audible_hz // f0) + 1):
        ideal = ideal_harmonic_amplitude(wave, k, pulse_width)
        if ideal < 1e-3 * a1:
            continue
        measured = np.sqrt(2.0 * spec.band_power(k * f0))
        err = abs(20.0 * np.log10(max(measured, floor) / ideal))
        err20 = max(err20, err)
        if k * f0 <= 10000.0:
            err10 = max(err10, err)

    return AliasResult(
        f0=f0,
        alias_total_dbc=10.0 * np.log10(max(alias_power, floor) / p1),
        alias_worst_dbc=10.0 * np.log10(max(worst, floor) / p1),
        harmonic_error_10k_db=err10,
        harmonic_error_20k_db=err20,
    )


def peak(x: np.ndarray) -> float:
    return float(np.max(np.abs(x))) if len(x) else 0.0


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(x)))) if len(x) else 0.0


def dc_offset(x: np.ndarray) -> float:
    return float(np.mean(x)) if len(x) else 0.0
