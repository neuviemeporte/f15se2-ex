# DOS preview

32bit DOS port of F-15 SE2 Ex

## Run

You need your own original F-15 Strike Eagle II game files. They are not
included in the download. Extract the DOS ZIP into a writable directory
alongside those files, keeping `CWSDPMI.EXE` beside `F15SE2EX.EXE`, then run:

```text
F15SE2EX.EXE
```

Alternatively, keep the game data in another directory:

```text
F15SE2EX.EXE --game C:\F15
```

Use `--nointro` to skip the logo/intro sequence. Back up your pilot records
before testing: the game writes to its normal game-data directory.

For DOSBox on Linux, with the executable and game files in separate folders:

```text
mount c /path/to/dos-release
mount d /path/to/original-game
c:
F15SE2EX.EXE --game D:/
```

This is a 32-bit DJGPP build; it needs a DPMI host and VGA/VESA graphics. Configure `BLASTER` for your Sound Blaster-compatible card on real hardware.

## Controls and audio

Connect the joystick before starting DOSBox. The DOS driver exposes two axes
and four buttons; the X/Y axes steer, button 1 fires the cannon, and button 2
fires missiles. Those two buttons also confirm/cancel menu choices.
Buttons 3 and 4 retain afterburner and weapon-cycle actions respectively.
Keyboard controls are unchanged.

Explicit joystick calibration is not included yet. The SDL DOS driver
currently learns its axis range while the stick moves.

FM music and effects are written straight to the AdLib-compatible OPL at port
388h that every Sound Blaster carries; the software OPL emulator is not built
into the DOS binary by default. For a Sound Blaster-compatible card without an
FM part, build with `-DFM_EMULATION=ON` to synthesize FM into the PCM output
instead (much more CPU). Speech always uses SDL's Sound Blaster audio output.
This is a game-side DOS backend, not an SDL workaround.

## Temporary SDL workarounds

- Joystick mapping: `src/joystick.c` supplies bindings for the controls the
  DOS driver actually exposes. The generic mapping otherwise reads missing
  trigger axes as half-pressed. Tracked by
  https://github.com/libsdl-org/SDL/issues/16289.

The upstream fix is not assumed to be available merely because it was
reported. When updating SDL, check the linked issue, test idle joystick
input, then remove the superseded workaround.

## Build

With DJGPP's cross-compilers on `PATH` and a DJGPP CMake toolchain file:

```sh
cmake -S . -B build-dos \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/i586-pc-msdosdjgpp.cmake \
  -DCMAKE_BUILD_TYPE=Release -DDOWNLOAD_DEPENDENCIES=ON
cmake --build build-dos -j4
```

The output is `build-dos/f15se2ex.exe`. Build output and original game assets
must not be committed. The release's source archive includes dependency
sources and additional instructions for rebuilding without fetching them.

## Redistribution

The binary ZIP includes the game's MIT license and the SDL, Nuked OPL3 and
QuickDigest5 licenses. The accompanying source archive contains the matching
game and dependency source used to build the binary, including the SDL patch.
QuickDigest5 is GPLv3 and Nuked OPL3 is LGPLv2.1; preserve those terms when
redistributing the linked build. This does not grant rights to original
commercial game assets.

CWSDPMI release 7 is included unmodified with its documentation. You have the
right to receive its source and binary updates:
https://sandmann.dotster.com/cwsdpmi/
