#!/usr/bin/env python3
"""Inspect an F-15 SE II blackbox log around a deterministic tick/time window."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

TICKS_PER_SECOND = 60


@dataclass(frozen=True)
class Event:
    tick: int
    kind: str
    fields: tuple[str, ...]


def parse_tick_or_time(value: str) -> int:
    displayed = int(value, 0)
    if displayed < 0 or displayed % 100 >= TICKS_PER_SECOND:
        raise argparse.ArgumentTypeError("displayed time must end in 00..59")
    return displayed // 100 * TICKS_PER_SECOND + displayed % 100


def tick_time(tick: int) -> str:
    return str(tick // TICKS_PER_SECOND * 100 + tick % TICKS_PER_SECOND)


def parse_log(path: Path) -> tuple[int | None, list[Event]]:
    seed: int | None = None
    events: list[Event] = []
    with path.open("r", encoding="utf-8") as f:
        header = f.readline().strip()
        if header != "F15SE2_BLACKBOX 5":
            raise SystemExit(f"unsupported blackbox header: {header!r}")
        for lineno, line in enumerate(f, start=2):
            parts = line.strip().split()
            if not parts:
                continue
            kind = parts[0]
            if kind == "seed" and len(parts) == 2:
                seed = int(parts[1], 0)
                continue
            if kind == "build_version":
                continue
            if kind == "mutable_file":
                continue
            if kind in {"phase", "rng_seed", "rng", "key", "axes", "frame",
                        "marker", "state", "render_scene", "render_object",
                        "render_line", "render_hash"} and len(parts) >= 2:
                try:
                    tick = int(parts[1], 0)
                except ValueError as exc:
                    raise SystemExit(f"{path}:{lineno}: bad tick: {parts[1]!r}") from exc
                events.append(Event(tick=tick, kind=kind, fields=tuple(parts[2:])))
                continue
            raise SystemExit(f"{path}:{lineno}: unknown blackbox line: {line.rstrip()!r}")
    return seed, events


def format_event(event: Event) -> str:
    if event.kind == "key" and len(event.fields) == 2:
        detail = f"pump={event.fields[0]} word=0x{int(event.fields[1], 16):04x}"
    elif event.kind == "axes" and len(event.fields) == 4:
        detail = f"raw=({event.fields[0]},{event.fields[1]}) joy=({event.fields[2]},{event.fields[3]})"
    elif event.kind == "rng" and event.fields:
        detail = f"value={event.fields[0]}"
    elif event.kind == "rng_seed" and event.fields:
        detail = f"seed={event.fields[0]}"
    elif event.kind == "phase" and event.fields:
        detail = event.fields[0]
    elif event.kind == "frame" and len(event.fields) >= 2:
        detail = f"frame={event.fields[0]} hash=0x{int(event.fields[1], 16):08x}"
    elif event.kind == "marker" and len(event.fields) == 4:
        detail = f"{event.fields[0]}({event.fields[1]},{event.fields[2]},{event.fields[3]})"
    elif event.kind == "state" and len(event.fields) == 3:
        detail = f"step={event.fields[0]} subsystem={event.fields[1]} hash=0x{int(event.fields[2], 16):08x}"
    elif event.kind == "render_hash" and len(event.fields) == 5:
        detail = (f"frame={event.fields[0]} scene={event.fields[1]} "
                  f"objects={event.fields[2]} lines={event.fields[3]} "
                  f"hash=0x{int(event.fields[4], 16):08x}")
    else:
        detail = " ".join(event.fields)
    return f"{event.tick:8d} {tick_time(event.tick)} {event.kind:8s} {detail}"


def summarize(events: Iterable[Event]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for event in events:
        counts[event.kind] = counts.get(event.kind, 0) + 1
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--around-tick", type=parse_tick_or_time, default=None,
                        help="center at displayed time (seconds*100 + tick), e.g. 20545")
    parser.add_argument("--from-tick", dest="from_tick", type=parse_tick_or_time, default=None,
                        help="start at displayed time (seconds*100 + tick)")
    parser.add_argument("--to-tick", dest="to_tick", type=parse_tick_or_time, default=None,
                        help="end at displayed time (seconds*100 + tick)")
    parser.add_argument("--before", type=int, default=180, help="ticks before --around-tick")
    parser.add_argument("--after", type=int, default=180, help="ticks after --around-tick")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    args = parser.parse_args()

    seed, events = parse_log(args.log)
    if args.around_tick is not None:
        start = max(0, args.around_tick - args.before)
        end = args.around_tick + args.after
    else:
        start = 0 if args.from_tick is None else args.from_tick
        end = (events[-1].tick if events else 0) if args.to_tick is None else args.to_tick

    window = [event for event in events if start <= event.tick <= end]
    if args.json:
        print(json.dumps({
            "file": str(args.log),
            "seed": seed,
            "start_tick": start,
            "end_tick": end,
            "counts": summarize(events),
            "events": [
                {"tick": e.tick, "time": tick_time(e.tick), "kind": e.kind, "fields": list(e.fields)}
                for e in window
            ],
        }, indent=2))
        return 0

    max_tick = events[-1].tick if events else 0
    print(f"file: {args.log}")
    print(f"seed: {seed if seed is not None else 'unknown'}")
    print(f"recorded ticks: 0..{max_tick} ({tick_time(max_tick)})")
    print("event counts: " + ", ".join(f"{k}={v}" for k, v in sorted(summarize(events).items())))
    print(f"window: {start}..{end} ({tick_time(start)}..{tick_time(end)})")
    for event in window:
        print(format_event(event))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
