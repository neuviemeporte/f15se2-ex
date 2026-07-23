"""Digitized cue WAV replacement validation."""

from __future__ import annotations

import sys
import wave
from pathlib import Path

from .sounds import ASOUND_SAMPLE_CUES, DEFAULT_SAMPLE_RATE, DIGITIZED_SAMPLE_BLOB
from .validation_compare import compare_wav_sample_rate, compare_wav_samples
from .validation_compare_music import compare_intro_music_json, compare_intro_music_midi

__all__ = ["validate_sound_replacements"]


def _read_pcm8_mono_wav(path: Path) -> tuple[int, bytes]:
    """Read pcm8 mono wav."""
    with wave.open(str(path), "rb") as wav:
        if wav.getnchannels() != 1:
            raise ValueError(f"expected mono WAV: {path}")
        if wav.getsampwidth() != 1:
            raise ValueError(f"expected unsigned 8-bit PCM WAV: {path}")
        if wav.getcomptype() != "NONE":
            raise ValueError(f"expected uncompressed PCM WAV: {path}")
        sample_rate = int(wav.getframerate())
        samples = wav.readframes(wav.getnframes())
    return sample_rate, samples


def _repo_root_from_tools() -> Path:
    """Perform the repo root from tools asset-processing operation."""
    return Path(__file__).resolve().parents[3]


def _validate_intro_music_export(sound_root: Path, require_all: bool) -> tuple[int, int]:
    """Validate intro music export against runtime requirements."""
    checked = 0
    failed = 0
    json_path = sound_root / "intro_music.asound.json"
    midi_path = sound_root / "intro_music.mid"
    if not json_path.exists() and not midi_path.exists():
        if require_all:
            print(f"missing intro music ASOUND JSON: {json_path}", file=sys.stderr)
            print(f"missing intro music MIDI preview: {midi_path}", file=sys.stderr)
            return checked, 2
        return checked, failed

    if not json_path.exists():
        print(f"missing intro music ASOUND JSON: {json_path}", file=sys.stderr)
        failed += 1
    else:
        checked += 1
        source_text = (_repo_root_from_tools() / "src" / "asound" / "asound_model.c").read_text(encoding="utf-8")
        errors = compare_intro_music_json(json_path, source_text)
        for error in errors:
            print(error, file=sys.stderr)
        failed += len(errors)

    if midi_path.exists():
        checked += 1
    midi_errors = compare_intro_music_midi(midi_path)
    for error in midi_errors:
        print(error, file=sys.stderr)
    failed += len(midi_errors)
    return checked, failed


def validate_sound_replacements(
    input_root: Path,
    output_root: Path,
    require_all: bool,
    loadability_only: bool = False,
) -> tuple[int, int]:
    """Validate sound replacements against runtime requirements."""
    blob_path = input_root / DIGITIZED_SAMPLE_BLOB
    sound_root = output_root / "sounds"
    checked = 0
    failed = 0
    checked_wav_paths: set[Path] = set()

    blob = blob_path.read_bytes() if blob_path.exists() else None
    for cue in ASOUND_SAMPLE_CUES:
        cue_id = str(cue["id"])
        wav_path = sound_root / f"{cue_id}.wav"
        if not wav_path.exists():
            if require_all:
                print(f"missing replacement WAV: {wav_path}", file=sys.stderr)
                failed += 1
            continue

        checked += 1
        checked_wav_paths.add(wav_path.resolve())
        legacy_samples = b""
        if blob is not None:
            start = int(cue["source_start"])
            end = min(int(cue["source_end_inclusive"]) + 1, len(blob))
            legacy_samples = blob[start:end]
        try:
            sample_rate, modern_samples = _read_pcm8_mono_wav(wav_path)
        except Exception as exc:
            print(f"{wav_path}: {exc}", file=sys.stderr)
            failed += 1
            continue

        if sample_rate <= 0:
            print(f"{wav_path}: invalid sample rate {sample_rate}", file=sys.stderr)
            failed += 1
        if not modern_samples:
            print(f"{wav_path}: WAV contains no samples", file=sys.stderr)
            failed += 1
            continue
        if not loadability_only and blob is not None:
            rate_error = compare_wav_sample_rate(str(wav_path), DEFAULT_SAMPLE_RATE, sample_rate)
            if rate_error:
                print(rate_error, file=sys.stderr)
                failed += 1
            sample_error = compare_wav_samples(str(wav_path), legacy_samples, modern_samples)
            if sample_error:
                print(sample_error, file=sys.stderr)
                failed += 1
        elif not loadability_only and blob is None:
            print(
                f"warning: {blob_path} is missing; validated {wav_path} loadability but skipped legacy sample comparison",
                file=sys.stderr,
            )

    if loadability_only and sound_root.exists():
        for wav_path in sorted(sound_root.glob("voice_cue_*.wav")):
            if wav_path.resolve() in checked_wav_paths:
                continue
            checked += 1
            try:
                sample_rate, modern_samples = _read_pcm8_mono_wav(wav_path)
            except Exception as exc:
                print(f"{wav_path}: {exc}", file=sys.stderr)
                failed += 1
                continue
            if sample_rate <= 0:
                print(f"{wav_path}: invalid sample rate {sample_rate}", file=sys.stderr)
                failed += 1
            if not modern_samples:
                print(f"{wav_path}: WAV contains no samples", file=sys.stderr)
                failed += 1

    music_checked, music_failed = _validate_intro_music_export(sound_root, require_all)
    checked += music_checked
    failed += music_failed

    return checked, failed
