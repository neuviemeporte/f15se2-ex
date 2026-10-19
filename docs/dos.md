# DOS preview

32bit DOS port of F-15 SE2 Ex

## Run

You need your own original F-15 Strike Eagle II game files. They are not
included in the download. Extract the DOS ZIP into a writable directory
alongside those files, then run:

```text
F15SE2EX.EXE
```

Alternatively, keep the game data in another directory:

```text
F15SE2EX.EXE --game C:\F15
```

Use `--nointro` to skip the logo/intro sequence. Back up your pilot records
before testing: the game writes to its normal game-data directory.

This is a 32-bit DJGPP build; it needs a DPMI host and VGA/VESA graphics.
DOS itself doesn't provide a DPMI host, so get CWSDPMI (the
`csdpmi7b.zip` binary package from https://sandmann.dotster.com/cwsdpmi/ or the
DJGPP `v2misc` directory on any DJGPP mirror) and put `CWSDPMI.EXE` next to
`F15SE2EX.EXE`. A Windows 9x DOS box already provides DPMI.

Sound uses the `BLASTER` environment variable to find your Sound
Blaster-compatible card. Emulators usually set it already; otherwise set it to
match your card's jumpers or setup utility, typically in `AUTOEXEC.BAT`:

```text
SET BLASTER=A220 I5 D1 H5 T6
```

`A` is the I/O port, `I` the IRQ, `D` the 8-bit DMA channel, `H` the 16-bit
DMA channel (Sound Blaster 16 only) and `T` the card type (6 = Sound Blaster 16).

## Controls and audio

Connect the joystick before launching the game. The DOS driver exposes two axes
and four buttons; the X/Y axes steer, button 1 fires the cannon, and button 2
fires missiles. Those two buttons also confirm/cancel menu choices.
Buttons 3 and 4 retain afterburner and weapon-cycle actions respectively.
Keyboard controls are unchanged.

The joystick calibrates itself: there is no calibration screen to sit
through. Move the stick around its full range once and the game learns its
limits.

A Sound Blaster or compatible card is required for music, effects and speech.

## Build

With DJGPP's cross-compilers on `PATH`:

```sh
cmake -S . -B build-dos \
  -DCMAKE_TOOLCHAIN_FILE=cmake/i586-pc-msdosdjgpp.cmake \
  -DCMAKE_BUILD_TYPE=Release -DDOWNLOAD_DEPENDENCIES=ON
cmake --build build-dos -j4
```

The output is `build-dos/f15se2ex.exe`. If `upx` or `upx-ucl` is installed, the
Release build is packed with it so the game fits on a single 1.44 MB floppy.
