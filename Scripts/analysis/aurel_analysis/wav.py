"""WAV loading for renders produced by aurel_render (32-bit float)."""

from __future__ import annotations

from pathlib import Path

import numpy as np
from scipy.io import wavfile


def read_wav(path: str | Path) -> tuple[np.ndarray, int]:
    """Returns (samples, sample_rate). samples is float64 with shape (frames, channels)."""
    sample_rate, data = wavfile.read(str(path))
    if data.dtype != np.float32:
        raise ValueError(f"{path}: expected 32-bit float WAV, got {data.dtype}")
    if data.ndim == 1:
        data = data[:, np.newaxis]
    return data.astype(np.float64), int(sample_rate)
