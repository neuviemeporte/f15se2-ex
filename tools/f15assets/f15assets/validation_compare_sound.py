"""Sound replacement comparison helpers."""

from __future__ import annotations

from .validation_compare_core import compare_byte_sequence


def compare_wav_samples(path: str, legacy_samples: bytes, modern_samples: bytes) -> str | None:
    return compare_byte_sequence(path, legacy_samples, modern_samples, "legacy", "wav", "WAV samples differ")


def compare_wav_sample_rate(path: str, expected_rate: int, actual_rate: int) -> str | None:
    if actual_rate == expected_rate:
        return None
    return f"{path}: WAV sample rate differs (expected={expected_rate}, wav={actual_rate})"
