# F-15 Strike Eagle 2 source port

<p align="center">
  <img src="screenshots/title.png" width="50%" alt="F-15 title screen">
</p>

This is a work in progress project to port, fix and enhance the [reconstructed source code](https://github.com/neuviemeporte/f15se2-re) of the Microprose game F-15 Strike Eagle 2 v451.03 (the definitive 1991 Desert Storm expansion disk version) for modern architectures.

Unlike the old project, whose aim was a bug-for-bug, instruction-level faithful recreation of the game for the orginal MS-DOS platform and the MS C v5.1 compiler, here we aim to keep as much of the game's spirit intact, while taking it forward into the 21st century with modern compilers and libraries, better graphics and enhanced features.

The repository was forked off from tag `v0.9.2`, and the idea is that the reconstruction will see occasional backports from this project which provide bugfixes, clarify or document the original code, but of course these will need to make sure to preserve the contract of instruction-level identity with the original.

The project is based on the [SDL3 library](https://github.com/libsdl-org/SDL/releases) for the graphical frontend.

Development journal: https://neuviemeporte.github.io/category/f15-se2

## Status (27.06.2026)

The entire game is playable, rendering and input handling is ported to SDL, sound works using Adlib emulation through [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3), joystick input is supported (not configurable right now).

## Screenshots

<div align="center">
  <table>
    <tr>
      <td align="center">
        <a href="screenshots/screen1.png"><img src="screenshots/screen1.png" width="150"></a>
      </td>
      <td align="center">
        <a href="screenshots/screen2.png"><img src="screenshots/screen2.png" width="150"></a>
      </td>
      <td align="center">
        <a href="screenshots/screen3.png"><img src="screenshots/screen3.png" width="150"></a>
      </td>
    </tr>
    <tr>
      <td align="center">
        <a href="screenshots/screen4.png"><img src="screenshots/screen4.png" width="150"></a>
      </td>
      <td align="center">
        <a href="screenshots/screen5.png"><img src="screenshots/screen5.png" width="150"></a>
      </td>
      <td align="center">
        <a href="screenshots/screen6.png"><img src="screenshots/screen6.png" width="150"></a>
      </td>
    </tr>
  </table>
</div>

## Completed enhancements over the original game

1. The original was limited to 15 FPS with a convoluted time scale implementation to make sure the game engine kept up with rendering. This has been eliminated, with the game engine being decoupled from rendering, so now it plays much smoother. 
1. Input loop has been upgraded to an SDL event pump which should make it deal with simultaneous inputs much bettern and improve general responsiveness.
1. The game originally supported 4 levels of detail (`0-3`, switchable with `Alt-D`), with the highest one still suffering from limited draw distance. An additional level of detail (`4`) has been implemented with unlimited draw distance, and enabled by default.
1. Rendering has been moved out of the bespoke software engine that was capped at `320x200` resolution (still available with `F15_RENDER=software` envvar) and into OpenGL (with MSAA), enabling higher resolutions and improved clarity and nicer graphics (particularly the horizon now has fog). At a later time, perhaps it will also be possible to upgrade the original software renderer to support higher resolutions.
1. The map, radar and target views have also been upgraded to high resolution 3D rendering (was done in 2D).
1. Air targets are now also selectable with the `T` key, just like ground targets.
1. Widescreen is supported, HUD scaling could still use improvements (too big)
1. 3D object occlusion was fixed, was difficult due to the engine handling the draw order and aspects of rendering in an unorthodox way, many models containing coplanar surfaces which would z-fight; solved by introducing depth bias.

## Planned features and improvements

These are things that were never part of, or were broken in the original that are planned to get fixed in this project.

1. Make the missiles more difficult to evade, as it's currently trivial (just beam them, i.e. put them on approx 90deg angle to the plane). Implement quasi-realistic missile energy management with self propelled/ballistic stages and gradual reduction in maneuverability. Denser air at lower altitudes should influence missile drag. Terrain masking should make missiles lose track.
1. Countermeasures (chaff/flare) are likewise too effective (100%) against missiles. Take missile aspect into account, e.g. chaff should not do much for a missile coming straight on, and flares should be less effective against a heat seeker missile coming from the rear.
1. Make enemy plane AI more capable, right now planes are barely a nuisance, slow, barely maneuvering, will rarely shoot missiles, not sure getting hit by gunfire is even possible.
1. Make the gun more predictable, right now it's spraying all over the place so it's difficult to tell where it's going. The gun and explosion effects were moved to 3D (they were 2D in the original game), but the point of impact does not really line up with the reticle, make bullets affected by gravity etc.
1. More realistic player aircraft handling, right now it's too responsive, turns too quickly.
1. Implement missile trails for better situational awareness/cool visuals.
1. Fix firing a missile at a new target (particularly air?) while the old target is still in the hud making the missile hit the old target.
1. Make the square bounding boxes marking objects like planes and missiles move less erratically when the object is close to the player.
1. In-game menu for configuration (keyboard/joystick binds, video resolution, turn engine sounds on and off, ...)
1. Better damage model for player aircraft, currently being hit by a missile only results in a small drop of maximum RPM. Simulate full/partial loss of stability, broken systems, weapons, hydraulics etc., up to instant destruction.
1. Better clouds and smoke effects, right now these are solid polygons in mid air.
1. More varied terrain and water, these are completely flat with an occasional pyramids that are supposed to represent mountains. It can continue to be flat shaded/polygon based to not change the look of the game too much, but we definitely need more vertices or textures.
1. Let player skip the ejection sequence and go straight to debriefing.
1. Implement a full 3D cockpit with 3DOF/6DOF head movement with the hat switch and/or TrackIR.
1. Scenario/model editor.
1. Multiplayer.
1. Port the game back to 32bit DOS.
1. VR support. 😈

## Known bugs

Problems with the game that were introduces by the port, and to the best of our knowledge are not present in the original.

1. Sometimes after starting a mission, planes and missiles are invisible (3d models missing?).
1. Fired missiles (Maverick only?) sometimes disappear near the target without a message ("Ineffective hit") or any other feedback.
1. It's sometimes impossible to lock some targets even when nearby, cycling targets just jumps over them.
1. The "BRG" bearing value in the target screen is sometimes a huge positive value (overflow?).
1. In the debriefing screen, plane names are only the long string e.g. "Flogger shot down".
1. When starting a new mission after a previous one has been completed, the sound for the previous flight's landing ("Nice landing") is played, looks as if the sound queue is not drained before terminating the previous mission?
1. When on the airfield/carrier, can see through to the ground on the sides of the view (exposed by widescreen support). Also, aircraft geometry sometimes flickers beneath the player.
1. Missile markers on radar are too short/thick and should be drawn on top everything else as the most important threat that should be very visible.
1. Pausing the game (`Alt-P`) appears to be busy waiting, CPU/GPU not idle.
1. Terrain is sometimes visible through "holes" in the cockpit

## Building

The build system is [CMake](https://cmake.org/download/) with [Ninja](https://github.com/ninja-build/ninja/releases) being used as the generator backend. It's been built successfully with both gcc and Clang. To build, run:

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

With this, I run `cmake --preset windows-clang` followed by `cmake --build build` to obtain `build/f15se2.exe`. 

Building with MSVC should also work.

## Running

After building, either drop the resulting binary into a directory with the game assets, use the `--game` command line option or the `F15SE2_DIR` environmental variable to set the assets' location.
