from __future__ import annotations

import hashlib
import json
import re
import struct
import wave
from pathlib import Path
from typing import Any, Dict, List

# The sound drivers program the PIT in the 8 kHz range. One recovered path uses
# a 0x98 low-byte divisor, which is about 7850 Hz on a 1.193182 MHz PIT.
DEFAULT_SAMPLE_RATE = 7850
SOUND_DRIVER_NAMES = ("NSOUND.EXE", "ASOUND.EXE", "ISOUND.EXE", "RSOUND.EXE", "TSOUND.EXE")
DIGITIZED_SAMPLE_BLOB = "F15DGTL.BIN"
ASOUND_TICK_HZ = 60

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
    """Perform the sha256 asset-processing operation."""
    return hashlib.sha256(data).hexdigest()


def write_unsigned_pcm8_wav(path: Path, samples: bytes, *, sample_rate: int = DEFAULT_SAMPLE_RATE) -> None:
    # F15DGTL.BIN is raw unsigned 8-bit PCM. WAV expects the same byte range for
    # 8-bit PCM, so no signedness conversion is needed here.
    """Write unsigned pcm8 wav."""
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(1)
        wav.setframerate(sample_rate)
        wav.writeframes(samples)


def decode_digitized_blob(data: bytes, *, sample_rate: int = DEFAULT_SAMPLE_RATE) -> Dict[str, Any]:
    """Decode digitized blob while preserving legacy semantics."""
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
            "sample split table. WAV files are authoritative for sample data and "
            "audio format; JSON is supplemental cue/index metadata."
        ),
    }


def infer_digitized_segments(data: bytes) -> List[Dict[str, Any]]:
    """Infer digitized segments."""
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
    # Preserve only portable identification metadata for the DOS sound drivers.
    # The executable bytes are not useful authoring files for customizers; they
    # remain in the original game directory for future driver research.
    """Describe preserved sound-driver fields whose meaning remains unknown."""
    return {
        "format": "F15_SOUND_DRIVER",
        "version": 1,
        "id": path.name,
        "status": "unresolved",
        "source_file": path.name,
        "source_offset": 0,
        "source_length": len(data),
        "source_bytes_sha256": _sha256(data),
        "codec": "driver_executable_unknown_tables",
        "notes": (
            "Sound-driver executable preserved for future reverse engineering. "
            "Effect mapping, synthesis parameters, and sample split tables are "
            "not decoded by this exporter yet."
        ),
    }


def _repo_root_from_tools() -> Path:
    """Perform the repo root from tools asset-processing operation."""
    return Path(__file__).resolve().parents[3]


def _extract_asound_array(source_text: str, name: str) -> List[int]:
    """Extract asound array."""
    match = re.search(
        rf"const\s+AsoundU8\s+{re.escape(name)}\[\]\s*=\s*\{{(?P<body>.*?)\}};",
        source_text,
        re.S,
    )
    if not match:
        raise ValueError(f"missing ASOUND stream array {name}")
    values: List[int] = []
    for token in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", match.group("body")):
        values.append(int(token, 0) & 0xFF)
    return values


def _decode_asound_stream(stream: List[int], voice: int, *, max_events: int = 10000) -> List[Dict[str, Any]]:
    """Decode asound stream while preserving legacy semantics."""
    pos = 0
    tick = 0
    loop_pos = 0
    loop_count = 0
    keyoff_gap = 1
    events: List[Dict[str, Any]] = []

    for _ in range(max_events):
        if pos >= len(stream):
            events.append({"tick": tick, "voice": voice, "type": "stream_end", "reason": "eof"})
            return events
        op = stream[pos]
        pos += 1
        if op < 0xF8:
            if pos >= len(stream):
                events.append({"tick": tick, "voice": voice, "type": "decode_error", "reason": "truncated_note"})
                return events
            duration = stream[pos]
            pos += 1
            if op == 0 and duration == 0:
                events.append({"tick": tick, "voice": voice, "type": "stream_end", "reason": "zero_note"})
                return events
            # The runtime schedules key-off before the full duration. Preserve
            # both values so JSON is lossless enough for a future native loader;
            # MIDI uses sounding_ticks for note length.
            sounding_ticks = max(1, duration - keyoff_gap) if duration > keyoff_gap else 1
            events.append(
                {
                    "tick": tick,
                    "voice": voice,
                    "type": "note",
                    "note": op,
                    "duration_ticks": duration,
                    "sounding_ticks": sounding_ticks,
                    "keyoff_gap_ticks": keyoff_gap,
                }
            )
            tick += duration
            continue
        if op == 0xF8:
            period = stream[pos] if pos < len(stream) else 0
            step = stream[pos + 1] if pos + 1 < len(stream) else 0
            pos += 2
            events.append({"tick": tick, "voice": voice, "type": "volume_fade", "period": period, "step": struct.unpack("b", bytes([step]))[0]})
        elif op == 0xF9:
            value = stream[pos] if pos < len(stream) else 0
            pos += 1
            events.append({"tick": tick, "voice": voice, "type": "volume", "value": value})
        elif op == 0xFA:
            value = stream[pos] if pos < len(stream) else 0
            pos += 1
            events.append({"tick": tick, "voice": voice, "type": "pitch_delta", "value": struct.unpack("b", bytes([value]))[0]})
        elif op == 0xFB:
            keyoff_gap = stream[pos] if pos < len(stream) else 0
            pos += 1
            events.append({"tick": tick, "voice": voice, "type": "keyoff_gap", "ticks": keyoff_gap})
        elif op == 0xFC:
            value = stream[pos] if pos < len(stream) else 0
            pos += 1
            events.append({"tick": tick, "voice": voice, "type": "instrument", "value": value})
        elif op == 0xFD:
            events.append({"tick": tick, "voice": voice, "type": "stream_end", "reason": "callback_or_end"})
            return events
        elif op == 0xFE:
            loop_pos = pos
            events.append({"tick": tick, "voice": voice, "type": "loop_start", "stream_pos": loop_pos})
        elif op == 0xFF:
            value = stream[pos] if pos < len(stream) else 0
            pos += 1
            if loop_count == 0:
                loop_count = value
                pos = loop_pos
            else:
                loop_count -= 1
                if loop_count != 0:
                    pos = loop_pos
                else:
                    loop_pos = pos
            events.append({"tick": tick, "voice": voice, "type": "loop", "count": value, "remaining": loop_count})
    raise ValueError("ASOUND stream decode exceeded safety event limit")


def _write_varlen(value: int) -> bytes:
    """Encode one MIDI variable-length integer."""
    value = max(0, int(value))
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def _midi_track(events: List[tuple[int, bytes]]) -> bytes:
    """Wrap encoded MIDI events in a complete track chunk."""
    events = sorted(events, key=lambda item: item[0])
    data = bytearray()
    last = 0
    for tick, payload in events:
        data.extend(_write_varlen(tick - last))
        data.extend(payload)
        last = tick
    data.extend(b"\x00\xFF\x2F\x00")
    return b"MTrk" + struct.pack(">I", len(data)) + data


def _write_intro_midi(path: Path, voices: List[Dict[str, Any]]) -> None:
    """Write intro midi."""
    ppq = 480
    midi_ticks_per_asound_tick = 16  # 60 Hz ASOUND tick at 120 BPM.
    tracks = []
    meta = [
        (0, b"\xFF\x51\x03\x07\xA1\x20"),  # 120 BPM
        (0, b"\xFF\x58\x04\x04\x02\x18\x08"),
    ]
    tracks.append(_midi_track(meta))
    for voice in voices:
        channel = int(voice["voice"]) % 16
        track_events: List[tuple[int, bytes]] = []
        current_program = 0
        for event in voice["events"]:
            tick = int(event["tick"]) * midi_ticks_per_asound_tick
            if event["type"] == "instrument":
                current_program = int(event["value"]) % 128
                track_events.append((tick, bytes([0xC0 | channel, current_program])))
            elif event["type"] == "note" and int(event["note"]) > 0:
                note = max(0, min(127, int(event["note"])))
                velocity = 96
                end_tick = tick + int(event["sounding_ticks"]) * midi_ticks_per_asound_tick
                track_events.append((tick, bytes([0x90 | channel, note, velocity])))
                track_events.append((end_tick, bytes([0x80 | channel, note, 0])))
        tracks.append(_midi_track(track_events))
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), ppq)
    path.write_bytes(header + b"".join(tracks))


def export_intro_music(output_root: Path, *, repo_root: Path | None = None) -> Dict[str, Any]:
    """Export intro music to an editable modern format."""
    repo = repo_root or _repo_root_from_tools()
    model_path = repo / "src" / "asound" / "asound_model.c"
    source_text = model_path.read_text(encoding="utf-8")
    output_root.mkdir(parents=True, exist_ok=True)

    voices: List[Dict[str, Any]] = []
    for phase in ("intro", "release"):
        for voice in range(6):
            name = f"asound_{phase}_voice{voice}"
            stream = _extract_asound_array(source_text, name)
            voices.append(
                {
                    "phase": phase,
                    "voice": voice,
                    "source_symbol": name,
                    "stream_bytes": stream,
                    "events": _decode_asound_stream(stream, voice),
                }
            )

    json_path = output_root / "intro_music.asound.json"
    midi_path = output_root / "intro_music.mid"
    record = {
        "format": "F15_ASOUND_MUSIC",
        "version": 1,
        "source": "recovered_asound_model",
        "source_file": "src/asound/asound_model.c",
        "tick_hz": ASOUND_TICK_HZ,
        "runtime_authoritative": "intro_music.asound.json",
        "midi_preview": midi_path.name,
        "notes": "ASOUND JSON preserves driver stream commands. MIDI is a best-effort note preview and does not preserve OPL instrument timbre.",
        "voices": voices,
    }
    json_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    _write_intro_midi(midi_path, [voice for voice in voices if voice["phase"] == "intro"])
    return {
        "id": "intro_music",
        "status": "decoded_recovered_asound_streams",
        "json": json_path.name,
        "midi": midi_path.name,
        "runtime_authoritative": json_path.name,
        "notes": "MIDI is for editing/preview; ASOUND JSON is the lossless replacement candidate.",
    }


def export_sounds(
    asset_root: Path,
    output_root: Path,
    *,
    sample_rate: int = DEFAULT_SAMPLE_RATE,
    include_raw_blob: bool = False,
    include_metadata: bool = False,
) -> Dict[str, Any]:
    """Export sounds to an editable modern format."""
    output_root.mkdir(parents=True, exist_ok=True)
    exported: List[Dict[str, Any]] = []

    # Keep repeated default conversions honest: optional metadata/reference
    # files from older runs must not linger and look like required customizer
    # assets. The cue WAV files are the default authoring/runtime surface.
    if not include_metadata:
        for stale_path in output_root.glob("voice_cue_*.json"):
            stale_path.unlink()
        for stale_path in output_root.glob("*_driver.json"):
            stale_path.unlink()
        stale_index = output_root / "sounds.json"
        if stale_index.exists():
            stale_index.unlink()
    if not include_raw_blob:
        for stale_name in ("f15dgtl_raw.wav", "f15dgtl_raw.json"):
            stale_path = output_root / stale_name
            if stale_path.exists():
                stale_path.unlink()

    blob_path = asset_root / DIGITIZED_SAMPLE_BLOB
    if blob_path.exists():
        data = blob_path.read_bytes()
        segments = infer_digitized_segments(data)

        if include_raw_blob:
            wav_path = output_root / "f15dgtl_raw.wav"
            json_path = output_root / "f15dgtl_raw.json"
            write_unsigned_pcm8_wav(wav_path, data, sample_rate=sample_rate)
            record = decode_digitized_blob(data, sample_rate=sample_rate)
            # Keep JSON portable: source_file names the original game asset, and
            # wav/json references are relative to this sound export directory.
            record["source_file"] = DIGITIZED_SAMPLE_BLOB
            record["wav_file"] = wav_path.name
            json_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            exported.append(
                {
                    "id": "F15DGTL.BIN",
                    "status": record["status"],
                    "wav": wav_path.name,
                    "json": json_path.name,
                    "notes": "Optional full-blob reference export; runtime replacement uses voice_cue_*.wav files.",
                }
            )

        cue_exports: List[Dict[str, Any]] = []
        for segment in segments:
            start = int(segment["source_offset"])
            end = start + int(segment["source_length"])
            segment_wav_path = output_root / f"{segment['id']}.wav"
            write_unsigned_pcm8_wav(segment_wav_path, data[start:end], sample_rate=sample_rate)
            cue_export = {
                "id": segment["id"],
                "wav": segment_wav_path.name,
                "boundary_status": segment["boundary_status"],
            }
            if include_metadata:
                segment_json_path = output_root / f"{segment['id']}.json"
                segment_record = {
                    "format": "F15_SOUND_SEGMENT",
                    "version": 1,
                    "id": segment["id"],
                    "status": "decoded_driver_recovered_cue",
                    "source_file": DIGITIZED_SAMPLE_BLOB,
                    "parent_blob": "F15DGTL.BIN",
                    "wav_file": segment_wav_path.name,
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
                }
                segment_json_path.write_text(
                    json.dumps(segment_record, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
                cue_export["json"] = segment_json_path.name
            cue_exports.append(cue_export)

        exported.append(
            {
                "id": "digitized_voice_cues",
                "status": "decoded_driver_recovered_cues",
                "source_file": DIGITIZED_SAMPLE_BLOB,
                "authoring_source": "voice_cue_*.wav",
                "runtime_authoritative": "voice_cue_*.wav",
                "notes": "Edit the individual cue WAVs for customization. No full sound blob is needed by the replacement runtime; use --include-raw-blob only for reverse-engineering reference output.",
                "segments": cue_exports,
            }
        )

    if include_metadata:
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
                    "json": json_path.name,
                }
            )

    exported.append(export_intro_music(output_root))

    return {
        "format": "F15_SOUND_EXPORT_INDEX",
        "version": 1,
        "source": "game_asset_directory",
        "sample_rate": sample_rate,
        "metadata_included": include_metadata,
        "raw_blob_included": include_raw_blob,
        "exported": exported,
    }
