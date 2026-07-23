"""ASOUND music replacement comparison helpers."""

from __future__ import annotations

import json
import re
from pathlib import Path


def extract_asound_array(source_text: str, name: str) -> list[int]:
    """Extract asound array."""
    match = re.search(
        rf"const\s+AsoundU8\s+{re.escape(name)}\[\]\s*=\s*\{{(?P<body>.*?)\}};",
        source_text,
        re.S,
    )
    if not match:
        raise ValueError(f"missing ASOUND stream array {name}")
    return [int(token, 0) & 0xFF for token in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", match.group("body"))]


def compare_intro_music_json(json_path: Path, source_text: str) -> list[str]:
    """Compare intro music json and report semantic mismatches."""
    errors: list[str] = []
    try:
        payload = json.loads(json_path.read_text(encoding="utf-8"))
    except Exception as exc:
        return [f"{json_path}: {exc}"]

    voices = payload.get("voices", [])
    if payload.get("format") != "F15_ASOUND_MUSIC":
        errors.append(f"{json_path}: wrong music JSON format")
    if not isinstance(voices, list) or len(voices) != 12:
        errors.append(f"{json_path}: expected 12 intro/release voices")
        return errors

    by_symbol = {voice.get("source_symbol"): voice for voice in voices if isinstance(voice, dict)}
    for phase in ("intro", "release"):
        for voice_id in range(6):
            symbol = f"asound_{phase}_voice{voice_id}"
            voice = by_symbol.get(symbol)
            if not isinstance(voice, dict):
                errors.append(f"{json_path}: missing voice {symbol}")
                continue
            expected_stream = extract_asound_array(source_text, symbol)
            actual_stream = [int(value) & 0xFF for value in voice.get("stream_bytes", [])]
            if actual_stream != expected_stream:
                errors.append(f"{json_path}: stream bytes differ for {symbol}")
            if not isinstance(voice.get("events"), list) or not voice["events"]:
                errors.append(f"{json_path}: missing decoded events for {symbol}")
    return errors


def compare_intro_music_midi(midi_path: Path) -> list[str]:
    """Compare intro music midi and report semantic mismatches."""
    if not midi_path.exists():
        return [f"missing intro music MIDI preview: {midi_path}"]
    data = midi_path.read_bytes()
    if len(data) < 14 or not data.startswith(b"MThd"):
        return [f"{midi_path}: invalid MIDI header"]
    return []
