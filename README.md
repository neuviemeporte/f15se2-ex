# F-15 Strike Eagle 2 source port

This is a work in progress project to port, fix and enhance the [reconstructed source code](https://github.com/neuviemeporte/f15se2-re) of the Microprose game F-15 Strike Eagle 2 v451.03 (the definitive 1991 Desert Storm expansion disk version) on modern architectures.

Unlike the old project, whose aim was a bug-for-bug, instruction-level faithful recreation of the game for the orginal MS-DOS platform and the MS C v5.1 compiler, here we aim to keep as much of the game's spirit intact, while taking it forward into the 21st century with modern language features, better graphics and enhanced features.

The repository was forked off from tag `v0.9.2`, and the idea is that the reconstruction might see occasional backports from this project which clarify or document the original code, but of course these will need to make sure to preserve the reconstruction's contract of instruction-level identity with the original.

The project is based on the [SDL3 library](https://github.com/libsdl-org/SDL/releases) for the graphical frontend. Initially, we're going to keep the original, software-based 3d rendering engine that outputs to a flat framebuffer, but the goal is to switch to a modern 3D rendering API like OpenGL or Vulkan at some point in the future.

## Status (27.06.2026)

The entire game is playable, software rendering is ported to SDL, sound works using Adlib emulation through [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3), joystick input is supported (not configurable right now). 

## Current enahncements over the original game

1. The original was limited to 15 FPS with a convoluted time scale implementation to make sure the game engine kept up. This has been mostly eliminated, with the game engine being decoupled from rendering, so now it plays much smoother. 
2. Input loop has been upgraded to an SDL event pump which should make it deal with multiple inputs and general responsiveness much better.
3. The game originally supported 4 levels of detail (`0-3`, switchable with `Alt-D`), with the highest one still suffering from limited draw distance. An additional level of detail (`4`) has been implemented with unlimited draw distance, and enabled by default.

## Planned features and improvements

### Architectural

1. Moving rendering out of the bespoke software engine and into a modern 3D API such as OpenGL or Vulkan, enabling higher resolutions along the way since the game is currently capped at `320x200`, preferably keeping the ability to switch between the two in real time. At a later time, perhaps it will also be possible to upgrade the original software rendering to custom resolutions.
2. Switching the internal physics calculations from fixed-point numbers to native hardware float for better precision and reduced jitterness.
3. Port the game back to 32bit DOS.
4. VR support. :)

### Game features

1. Implement a full 3D cockpit with 3DOF/6DOF head movement with the hat switch and/or TrackIR.
2. Make the missiles more difficult to evade, as it's currently trivial (just beam them, i.e. put them on approx 90deg angle to the plane). Implement quasi-realistic self propelled/ballistic stages, have missile run out of energy and maneuverability when propellant has been burned off.
3. Make the gun more predictable, right now it's spraying all over the place. Show nice tracers, make them affected by gravity etc.
4. Implement missile trails for better situational awareness.
5. Make air targets also selectable with the `T` key, like ground targets.
6. In-game menu for configuration (keyboard/joystick binds, turn engine sounds on and off, ...)

## Known bugs

1. Missiles that have lost track should be shown as gray on the radar screen but aren't.
2. With the highest LOD4 selected, mountains sometimes disappear when close and plane is pitching up and down.
3. Fired missiles (Maverick only?) sometimes disappear near the target without any message ("Ineffective hit") or any other feedback.
4. It's sometimes impossible to lock targets even when nearby.
5. Some problems with input, `Alt-Q` to quit and `Alt-Enter` for fullscreen does not always register, especially during the intro. Clicking on the window close button does nothing.

## Building

The build system is [CMake](https://cmake.org/download/) with [Ninja](https://github.com/ninja-build/ninja/releases) being used as the generator backend. It is intended to be built with Clang, but gcc also seems to work. To build, run:

```
$ cmake --preset <preset-name> # only needed the first time around, or when making changes to cmake files
$ cmake --build build
```

The project includes the default preset `base-ninja` in `CMakePresets.json`. It's possible to manually override platform-specific values for the build using CMake's user presets.

### Linux

On Ubuntu, I got to build with the default preset. The only thing needed was to install SDL3 with apt:

```
$ sudo apt install libsdl3-dev
$ cmake --preset base-ninja
$ cmake --build build
```

### Windows

To build on Windows using [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), I use this `CMakeUserPresets.json`:

```
{
  "version": 3,
  "configurePresets": [
    {
      "name": "windows-clang",
      "inherits": "base-ninja",
      "displayName": "Local Windows Clang + SDL3",
      "environment": {
        "CC": "D:/utility/llvm-mingw-20260616-ucrt-x86_64/bin/clang.exe",
        "CXX": "D:/utility/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe"
      },
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_PREFIX_PATH": "D:/code/SDL3-3.4.10/x86_64-w64-mingw32"
      }
    }
  ]
}
```

With this, I run `cmake --preset windows-clang` followed by `cmake --build build` to obtain `build/f15se2.exe`. It also needs `SDL3.dll` from `SDL3-3.4.10\x86_64-w64-mingw32\bin` in the same directory to run.
