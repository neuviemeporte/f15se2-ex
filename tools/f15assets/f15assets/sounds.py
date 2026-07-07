from __future__ import annotations

import hashlib
import json
import wave
from pathlib import Path
from typing import Any, Dict, List

from .io import to_base64

# The sound drivers program the PIT in the 8 kHz range. One recovered path uses
# a 0x98 low-byte divisor, which is about 7850 Hz on a 1.193182 MHz PIT.
DEFAULT_SAMPLE_RATE = 7850
SOUND_DRIVER_NAMES = ("NSOUND.EXE", "ASOUND.EXE", "ISOUND.EXE", "RSOUND.EXE", "TSOUND.EXE")
DIGITIZED_SAMPLE_BLOB = "F15DGTL.BIN"

# ASOUND.EXE does not store a simple monotonic split list. The game calls
# audio_playSample(weaponIdx), and ASOUND dispatches only sample offsets 0, 2,
# and 4. Offset 2 rotates through up to three variants selected by audio_setup()
# from the loaded F15DGTL.BIN length. Ranges below are inclusive in the driver.
ASOUND_SAMPLE_CUES = (
    {
        "id": "voice_cue_000_sample0",
        "play_sample_arg": 0,
        "variant": None,
        "source_start": 0x0000,
        "source_end_inclusive": 0x31F3,
        "driver_case": "sample_play_case0",
    },
    {
        "id": "voice_cue_001_sample4",
        "play_sample_arg": 4,
        "variant": None,
        "source_start": 0x31F4,
        "source_end_inclusive": 0x4796,
        "driver_case": "sample_play_case2",
    },
    {
        "id": "voice_cue_002_sample2_variant0",
        "play_sample_arg": 2,
        "variant": 0,
        "source_start": 0x4797,
        "source_end_inclusive": 0x5C92,
        "driver_case": "sample_play_case1",
    },
    {
        "id": "voice_cue_003_sample2_variant1",
        "play_sample_arg": 2,
        "variant": 1,
        "source_start": 0x5C93,
        "source_end_inclusive": 0x6A1A,
        "driver_case": "sample_play_case1",
    },
    {
        "id": "voice_cue_004_sample2_variant2",
        "play_sample_arg": 2,
        "variant": 2,
        "source_start": 0x6A1B,
        "source_end_inclusive": 0x7D9D,
        "driver_case": "sample_play_case1",
    },
)


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_unsigned_pcm8_wav(path: Path, samples: bytes, *, sample_rate: int = DEFAULT_SAMPLE_RATE) -> None:
    # F15DGTL.BIN is raw unsigned 8-bit PCM. WAV expects the same byte range for
    # 8-bit PCM, so no signedness conversion is needed here.
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(1)
        wav.setframerate(sample_rate)
        wav.writeframes(samples)


def decode_digitized_blob(data: bytes, *, sample_rate: int = DEFAULT_SAMPLE_RATE) -> Dict[str, Any]:
    segments = infer_digitized_segments(data)
    return {
        "format": "F15_SOUND",
        "version": 1,
        "id": "F15DGTL.BIN",
        "status": "decoded_raw_pcm_blob",
        "source_file": DIGITIZED_SAMPLE_BLOB,
        "source_offset": 0,
        "source_length": len(data),
        "source_bytes_sha256": _sha256(data),
        "codec": "unsigned_pcm_u8",
        "container": "raw",
        "sample_rate": sample_rate,
        "sample_rate_status": "driver_recovered",
        "channels": 1,
        "bits_per_sample": 8,
        "sample_count": len(data),
        "duration_seconds": len(data) / float(sample_rate) if sample_rate > 0 else 0,
        "segments": segments,
        "notes": (
            "F15DGTL.BIN is exported as a single raw unsigned 8-bit PCM stream. "
            "Enumerated cue WAV files are emitted from ASOUND.EXE playback ranges. "
            "voiceCueThresholds in egdata.c are availability guards, not the full "
            "sample split table."
        ),
        "source_bytes_base64": to_base64(data),
    }


def infer_digitized_segments(data: bytes) -> List[Dict[str, Any]]:
    segments: List[Dict[str, Any]] = []
    for idx, cue in enumerate(ASOUND_SAMPLE_CUES):
        start = int(cue["source_start"])
        end = min(int(cue["source_end_inclusive"]) + 1, len(data))
        if end <= start:
            continue
        segments.append(
            {
                "id": str(cue["id"]),
                "index": idx,
                "play_sample_arg": cue["play_sample_arg"],
                "variant": cue["variant"],
                "source_offset": start,
                "source_length": end - start,
                "source_end_inclusive": end - 1,
                "boundary_status": "driver_recovered",
                "boundary_source": "ASOUND.EXE sample_play_case0/1/2 ranges",
                "driver_case": cue["driver_case"],
                "codec": "unsigned_pcm_u8",
            }
        )
    return segments


def unresolved_sound_driver_record(path: Path, data: bytes) -> Dict[str, Any]:
    # Preserve driver executables byte-for-byte in metadata. They contain the
    # actual hardware playback/synthesis logic, but the exporter only decodes
    # the external digitized blob today.
    return {
        "format": "F15_SOUND_DRIVER",
        "version": 1,
        "id": path.name,
        "status": "unresolved",
        "source_file": str(path),
        "source_offset": 0,
        "source_length": len(data),
        "source_bytes_sha256": _sha256(data),
        "codec": "driver_executable_unknown_tables",
        "notes": (
            "Sound-driver executable preserved for future reverse engineering. "
            "Effect mapping, synthesis parameters, and sample split tables are "
            "not decoded by this exporter yet."
        ),
        "source_bytes_base64": to_base64(data),
    }


def export_sounds(asset_root: Path, output_root: Path, *, sample_rate: int = DEFAULT_SAMPLE_RATE) -> Dict[str, Any]:
    output_root.mkdir(parents=True, exist_ok=True)
    exported: List[Dict[str, Any]] = []

    blob_path = asset_root / DIGITIZED_SAMPLE_BLOB
    if blob_path.exists():
        data = blob_path.read_bytes()
        wav_path = output_root / "f15dgtl_raw.wav"
        json_path = output_root / "f15dgtl_raw.json"
        write_unsigned_pcm8_wav(wav_path, data, sample_rate=sample_rate)
        record = decode_digitized_blob(data, sample_rate=sample_rate)
        record["source_file"] = str(blob_path)
        record["wav_file"] = str(wav_path)
        for segment in record["segments"]:
            start = int(segment["source_offset"])
            end = start + int(segment["source_length"])
            segment_wav_path = output_root / f"{segment['id']}.wav"
            segment_json_path = output_root / f"{segment['id']}.json"
            write_unsigned_pcm8_wav(segment_wav_path, data[start:end], sample_rate=sample_rate)
            segment_record = {
                "format": "F15_SOUND_SEGMENT",
                "version": 1,
                "id": segment["id"],
                "status": "decoded_driver_recovered_cue",
                "source_file": str(blob_path),
                "parent_blob": "F15DGTL.BIN",
                "wav_file": str(segment_wav_path),
                "source_offset": start,
                "source_length": end - start,
                "source_bytes_sha256": _sha256(data[start:end]),
                "codec": "unsigned_pcm_u8",
                "sample_rate": sample_rate,
                "sample_rate_status": "driver_recovered",
                "channels": 1,
                "bits_per_sample": 8,
                "sample_count": end - start,
                "duration_seconds": (end - start) / float(sample_rate) if sample_rate > 0 else 0,
                "boundary_status": segment["boundary_status"],
                "boundary_source": segment["boundary_source"],
                "play_sample_arg": segment["play_sample_arg"],
                "variant": segment["variant"],
                "driver_case": segment["driver_case"],
                "source_end_inclusive": segment["source_end_inclusive"],
                "source_bytes_base64": to_base64(data[start:end]),
            }
            segment["wav_file"] = str(segment_wav_path)
            segment["json_file"] = str(segment_json_path)
            segment_json_path.write_text(
                json.dumps(segment_record, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
        json_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        exported.append(
            {
                "id": "F15DGTL.BIN",
                "status": record["status"],
                "wav": str(wav_path),
                "json": str(json_path),
                "segments": [
                    {
                        "id": segment["id"],
                        "wav": segment["wav_file"],
                        "json": segment["json_file"],
                        "boundary_status": segment["boundary_status"],
                    }
                    for segment in record["segments"]
                ],
            }
        )

    for name in SOUND_DRIVER_NAMES:
        driver_path = asset_root / name
        if not driver_path.exists():
            continue
        data = driver_path.read_bytes()
        json_path = output_root / f"{driver_path.stem.lower()}_driver.json"
        record = unresolved_sound_driver_record(driver_path, data)
        json_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        exported.append(
            {
                "id": name,
                "status": "unresolved",
                "json": str(json_path),
            }
        )

    return {
        "format": "F15_SOUND_EXPORT_INDEX",
        "version": 1,
        "source": str(asset_root),
        "sample_rate": sample_rate,
        "exported": exported,
    }
