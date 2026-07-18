# Deterministic blackbox debugging

The blackbox mode records a deterministic run into a text log that can be
replayed later. It is intended for bug reports and investigation: record a run
where a problem happens, inspect the log to find the timeframe, then replay and
pause near that tick.

## What is recorded

- deterministic 60 Hz tick counter;
- program phase markers: `start`, `egame`, `end`;
- BIOS-style key/input events;
- virtual-stick and joystick axes;
- deterministic RNG seeds and generated RNG values;
- pre-overlay indexed-page checksums;
- build identity and the initial `HallFame` state;
- gameplay transition markers and subsystem hashes;
- backend-independent 3D scene submission hashes.

Frame checksums are recorded before the blackbox overlay is drawn, so the
debug timer itself does not affect divergence checks.

## Time notation

The overlay and all CLI/inspector `*-tick` arguments use **displayed time**, a
single decimal number calculated as `seconds * 100 + subtick`. The final two
digits are `00..59`. For example:

- displayed time `20545` means 205 seconds plus 45/60 second;
- its internal raw tick is `205 * 60 + 45 = 12345`;
- blackbox event lines store that raw tick (`12345`), while commands accept the
  displayed value (`20545`).

Use the overlay number directly. A colon form such as `205:45` is not accepted.

## Build

```bash
cmake -S . -B build -DDOWNLOAD_DEPENDENCIES=ON
cmake --build build -j2
```

## Record a run

Use the original game asset directory with `--game`.

```bash
./build/f15se2-ex \
  --game /path/to/f15 \
  --blackbox-record /tmp/f15-run.bb \
  --blackbox-seed 7
```

Notes:

- `--blackbox-seed` selects the deterministic seed; it defaults to `1`.
- The output log is text and can be opened in an editor.
- The top-left overlay uses the displayed-time notation described above.

## Replay a run

```bash
./build/f15se2-ex \
  --game /path/to/f15 \
  --blackbox-replay /tmp/f15-run.bb
```

During replay, real gameplay input is ignored. Window/system events such as
close, focus, and resize still work.

Record/replay sessions are read-only for persistent game state. They suppress
normal runtime writes such as `HallFame` pilot/progress saves, so a bad run
cannot mutate the pilot roster while you record or inspect it.

The log also carries the `HallFame` snapshot from record start, or an explicit
empty snapshot if the recorder had no roster file. Replay reads that captured
roster state from memory, so another developer does not need the same local
pilot file and replay will not overwrite theirs.

If replay diverges from the recording, the game logs blackbox errors for the
first mismatching RNG seed/value, indexed-page checksum, gameplay marker,
subsystem hash, or render-submission hash.

By default, replay rejects logs recorded by another build. Use
`--blackbox-replay-ignore-build` only when you intentionally want to find where
a newer source version starts diverging.

## Pause replay at a tick

Use this after you know roughly where the problem happened.

```bash
./build/f15se2-ex \
  --game /path/to/f15 \
  --blackbox-replay /tmp/f15-run.bb \
  --blackbox-pause-tick 20545
```

Use the number shown by the overlay directly. `20545` means 205 seconds plus
45 ticks; the game converts it internally to raw tick 12345.

### Fast-forward to an investigation window

Replay can run without host presentation or deliberate sleeps until a target
tick. Simulation, rendering logic, recorded input/RNG consumption, and frame
hash verification still run normally:

```bash
./build/f15se2-ex \
  --game /path/to/f15 \
  --blackbox-replay /tmp/f15-run.bb \
  --blackbox-fast-forward-tick 20000 \
  --blackbox-pause-tick 20545
```

The pause tick may equal or follow the fast-forward target. It cannot precede
it, because that would freeze time while host presentation was still disabled.

## Inspect a log without running the game

Show events around the same displayed time:

```bash
python3 tools/blackbox_inspect.py /tmp/f15-run.bb \
  --around-tick 20545 \
  --before 180 \
  --after 180
```

## Gameplay diagnostics

Blackbox recordings include deterministic gameplay diagnostics by default. They
are passive: diagnostics read existing state and renderer submissions but do not
change simulation state, input, RNG, or rendering.

### Event markers

The simulation-step hook records transitions rather than requiring hooks spread
through gameplay code:

- `sim_begin`: first captured in-flight simulation state.
- `view_change`: old and new `ViewMode` values.
- `target_change`: tracked object, air lock, or ground lock changed.
- `projectile_launch`: projectile slot became active, with weapon and target.
- `projectile_remove`: projectile slot became free, with its final identity.
- `mission_end`: mission state entered its terminal path.

Show markers near displayed time `20545`:

```bash
python3 tools/blackbox_inspect.py /tmp/f15-run.bb \
  --around-tick 20545 --before 300 --after 300
```

Marker lines are replay-checked. A different marker, argument, order, or tick is
reported as the first marker divergence.

### Subsystem hashes

Every authoritative simulation step records five independent hashes:

- `flight`: player position, attitude, speed, altitude, fuel, and damage state.
- `camera`: view mode/orientation, camera eye, target point, and camera distance.
- `objects`: all 20 moving-aircraft/AI object records, field by field.
- `weapons`: all 12 guided projectiles plus selected weapon and gun state.
- `target_mission`: target locks, target lead, mission state, remaining enemies,
  and score state.

Replay compares each subsystem separately. The first error identifies which
subsystem diverged, its simulation step, and expected/actual hash. This narrows
investigation before adding temporary logs to gameplay code.

### Manual diagnostic dumps

While blackbox debug, record, or replay mode is active, press `Ctrl+F10`. The
shortcut is host-only and is never inserted into the game's key buffer, so it
does not alter deterministic input. It writes to the current directory:

```text
blackbox-dump-20545.txt
blackbox-dump-20545.json
```

The human-readable text dump contains:

- raw tick and displayed time;
- build provenance, blackbox mode, input/RNG cursors, and timer state;
- current hashes and readable player/camera/target state;
- every simulation object and projectile slot;
- the current render frame's recent scene/object/line submissions;
- a count if the fixed 512-command diagnostic buffer overflowed.

The versioned JSON snapshot is intended for agents, scripts, and snapshot
comparison. It serializes fields individually, not as raw C memory, and contains
the player, camera, target/mission and weapon context together with every field
of all simulation objects, projectiles, projectile interpolation coordinates,
and bullet tracks. It also records build provenance, recorder/RNG cursor state,
timer and interpolation state, waypoints, target-selection slots, and the full
74-entry mission target table. Stable slot numbers make object arrays
straightforward to diff between two times or two builds.

#### Text dump format

The `.txt` file is a compact investigation report. Its sections appear in this
order:

- `raw_tick` and `displayed_time` identify the captured simulation instant.
- `provenance` identifies the running build and the build that created a replay.
- `blackbox` reports mode, configured seed, RNG state, frame index, input pump,
  and event cursors. Mode values are `0` off, `1` debug, `2` record, and `3`
  replay. Cursors use `consumed/total` notation.
- `timing` reports timer counters, simulation steps in the current presented
  frame, Q12 render interpolation alpha, and the game RNG seed.
- `subsystem` contains separate hashes for flight, objects, weapons, camera,
  and target/mission state.
- `flight`, `camera`, and `target` provide the most useful state in readable
  form.
- `object[00]` through `object[19]` and `projectile[00]` through
  `projectile[11]` preserve stable runtime slot numbers.
- `render` lists up to 512 recent backend-independent render submissions. `S`
  is a scene/camera command, `O` is an object submission, and `L` is a line.
  `dropped` reports commands omitted after the fixed diagnostic buffer filled.

The text report is deliberately selective. In particular, complete waypoint,
mission-target, bullet-track, and interpolation records belong in the JSON
snapshot.

#### JSON snapshot format

The `.json` file has `format` and `version` fields. Version `1` uses these
top-level sections:

- `provenance`: current and recorded build identifiers.
- `time`: raw tick and compact displayed time.
- `blackbox`: mode, input pump, configured seed, RNG state, frame index, and
  event cursors. Every cursor is `[consumed, total]`.
- `timing`: timer installation/counters, frame tick, simulation-step count,
  Q12 interpolation alpha, and game RNG seed.
- `flight`, `camera`, `target_mission`, and `weapons`: current high-level game
  state.
- `waypoints`: all 4 waypoint records.
- `target_slots`: both target-selection records.
- `map_targets`: all 74 mission-map target slots.
- `sim_objects`: all 20 simulation-object slots.
- `projectiles`: all 12 guided-projectile slots, including interpolation
  coordinates.
- `bullet_tracks`: all 20 bullet-track slots.

Values retain the game's native integer units and raw flag bitfields. The dump
does not convert coordinates to meters, angles to degrees, or flags to inferred
names. Inactive and free slots remain present so their indices stay stable in
diffs. An unsigned tick value of `4294967295` (`0xffffffff`) means that the
corresponding pause or fast-forward trigger is not configured.

The JSON snapshot does not duplicate the text report's subsystem hashes or
recent render-command list; use the files together. File names use compact
displayed time, while `time.raw_tick` stores the underlying 60 Hz tick. A second
dump at the same displayed time overwrites files with the same names. Scripts
should address fields by name and slots by index rather than depend on JSON key
ordering.

Useful queries include:

```bash
# Establish whether two snapshots came from equivalent replay state.
jq '{provenance, blackbox, timing}' blackbox-dump-20545.json

# Show active mission-map targets while retaining their slot numbers.
jq '.map_targets | to_entries[] | select(.value.active != 0)' \
  blackbox-dump-20545.json

# Inspect one object and one guided projectile.
jq '{object: .sim_objects[10], projectile: .projectiles[8]}' \
  blackbox-dump-20545.json
```

Snapshots are intentionally write-only. Loading a JSON file directly into live
structures would leave derived caches, model references, and other dependent
state inconsistent; implementing save/load state is a separate, more invasive
feature.

Use a dump to inspect a paused replay:

```bash
./build/f15se2-ex --game /path/to/f15 \
  --blackbox-replay /tmp/f15-run.bb \
  --blackbox-fast-forward-tick 20400 \
  --blackbox-pause-tick 20545
```

At the paused frame, press `Ctrl+F10`. Read `blackbox-dump-20545.txt` manually or
diff/process `blackbox-dump-20545.json` with tooling such as `jq`.

For cross-platform automation, prefer a tick-triggered dump over the keyboard
shortcut:

```bash
./build/f15se2-ex --game /path/to/f15 \
  --blackbox-replay /tmp/f15-run.bb \
  --blackbox-fast-forward-tick 20545 \
  --blackbox-dump-tick 20545 \
  --blackbox-pause-tick 20545
```

`--blackbox-dump-tick` uses the same displayed-time notation as fast-forward
and pause, emits once when that deterministic tick is reached, and does not
depend on SDL presentation or desktop keyboard-shortcut handling. It can also
be used with `--blackbox-debug` or `--blackbox-record`. `Ctrl+F10` remains a
convenience shortcut, but a desktop environment or laptop firmware may consume
that key combination. The first valid automatic dump time is `1`; displayed
time `0` is rejected because game state has not reached a capturable tick.

If `--blackbox-pause-tick` is omitted, replay continues after writing the dump.
For a single unattended investigation command, set fast-forward, dump, and
pause to the same displayed time as in the example above.

Compare snapshots from two builds or runs with stable key ordering:

```bash
jq -S . blackbox-dump-20545.json > /tmp/snapshot-a.json
jq -S . other/blackbox-dump-20545.json > /tmp/snapshot-b.json
diff -u /tmp/snapshot-a.json /tmp/snapshot-b.json
```

Check `provenance`, `blackbox.cursors`, and `timing` first. If those differ, the
remaining object differences may be consequences rather than the root cause.

### Render-command capture

Compact backend-independent render hashes are recorded by default. They cover
each `r3d` scene's camera parameters, model submissions, transforms, positions,
shadows, world-space lines, and line colors. This captures the commands before
OpenGL/software backend differences.

For geometry, visibility, or z-fighting investigation, record every command:

```bash
./build/f15se2-ex --game /path/to/f15 \
  --blackbox-record /tmp/f15-render.bb \
  --blackbox-capture-render
```

This produces a substantially larger log containing:

- `render_scene`: scene orientation, position, and main/sub-view flag;
- `render_object`: stable model-data offset, attitude, position, and shadow flag;
- `render_line`: both 3D endpoints and final palette color;
- `render_hash`: scene command counts and combined hash.

Detailed commands are for inspection; replay equivalence uses `render_hash`.
Because capture occurs in `r3d.c`, the same command stream is observed whether
the selected backend is `opengl1` or `software`.

### Suggested issue workflow

1. Record a run that reproduces the issue.
2. Note the displayed time or find the relevant marker with the inspector.
3. Replay with fast-forward to shortly before that time.
4. Compare the first subsystem or render-hash divergence, if any.
5. Pause at the problem time and press `Ctrl+F10` for readable state.
6. For geometry bugs, make a second short recording with
   `--blackbox-capture-render` and inspect the model/line submissions.
7. Apply the fix and replay the same recording as an integration regression.

Useful mappings:

- upside-down external view or broken bearing: `camera` and `target_change`;
- disappearing missile: `projectile_launch`, `projectile_remove`, and `weapons`;
- skipped target lock: `target_change` and `target_mission`;
- invisible aircraft: `objects` versus `render_object` submissions;
- missing stabilizers, flicker, or z-fighting: detailed render capture and model
  offsets, then inspect the corresponding model asset/decoder.

Audio state is intentionally outside this diagnostic layer. These dumps can
correlate gameplay events that request sounds, but they cannot diagnose mixer,
OPL, sample-playback, or sound-queue state directly.

Show events around the compact displayed time:

```bash
python3 tools/blackbox_inspect.py /tmp/f15-run.bb \
  --around-tick 20545
```

Show an explicit range:

```bash
python3 tools/blackbox_inspect.py /tmp/f15-run.bb \
  --from-tick 12000 \
  --to-tick 12300
```

Emit JSON for scripts or agents:

```bash
python3 tools/blackbox_inspect.py /tmp/f15-run.bb \
  --from-tick 12000 \
  --to-tick 12300 \
  --json
```

## Log format

The log starts with:

```text
F15SE2_BLACKBOX 7
seed 7
build_version v0.9.4-1-g25b4664
```

Event lines use internal raw ticks, not displayed-time notation:

```text
phase 0 start
timer_pump 0 0
key 120 487 1f73
axes 121 128 128 128 128
rng_seed 240 1234
rng 240 3558
frame 241 19 1a2b3c4d
mutable_file HallFame 258 <hex_bytes>
marker 240 view_change 128 132 0
state 240 17 camera 8adf5d9e
render_hash 240 19 1 12 3 1a2b3c4d
```

Field meanings:

- `phase <tick> <name>`: executable phase marker.
- `timer_pump <start_tick> <tick_count>`: number of native 60 Hz ticks produced
  by one timer-pump call. Zero-count entries preserve the exact polling/render
  iteration on which the next tick became visible. Replay rejects counts above
  the native catch-up limit.
- `key <tick> <input_pump> <bios_word_hex>`: BIOS-style key word and the exact
  shared-input polling call that observed it. The pump sequence remains ordered
  while static menu/death screens have their 60 Hz timer stopped.
- `axes <tick> <rawX> <rawY> <joyX> <joyY>`: virtual-stick and joystick axes.
- `rng_seed <tick> <seed>`: deterministic RNG reseed.
- `rng <tick> <value>`: deterministic 15-bit RNG output.
- `frame <tick> <frame_index> <hash>`: pre-overlay indexed-page checksum for a
  logical game presentation. Window expose/resize redraws are excluded.
- `build_version <version>`: `git describe` string compiled into the binary.
- `mutable_file HallFame <size> <hex_bytes>`: captured pilot roster.
- `marker <tick> <name> <a> <b> <c>`: gameplay transition and arguments.
- `state <tick> <sim_step> <subsystem> <hash>`: field-by-field subsystem hash.
- `render_scene`, `render_object`, and `render_line`: detailed submissions,
  present only when recording with `--blackbox-capture-render`.
- `render_hash <tick> <frame> <scene> <objects> <lines> <hash>`: compact scene
  submission summary used for replay comparison.

Replay and the inspector only accept the current `F15SE2_BLACKBOX 7` format.
Unknown log lines are errors. This is intentional: silently accepting stale or
malformed troubleshooting logs would make divergence analysis unreliable.

## Limitations

- This is replay-to-tick, not reverse execution. To “rewind”, replay from the
  start and pause at an earlier tick.
- Dumps are diagnostic snapshots, not complete save states and cannot be loaded
  back into the game.
- Replay is deterministic for the instrumented input/RNG/virtual-timer path.
  Frame checksums are passive diagnostics: they never resynchronize time or
  alter execution. They cover the indexed 2D page passed through logical game
  presents, not host-requested redraws or the full OpenGL framebuffer.
- Intro audio completion is asynchronous during normal play. Blackbox mode uses
  the same bounded wait expressed in virtual ticks, so host audio scheduling
  cannot change menu control flow.
- Gamepad repeat timing is recorded as resulting key/axis events. Replay does
  not need the same physical gamepad connected.
