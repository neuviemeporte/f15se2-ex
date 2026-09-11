# Nightly builds

Nightly packages are experimental builds of the default branch. They run the
behavior tests before publishing to this repository's GitHub Releases at
03:23 UTC each day. All four platforms must pass before a release is published:
Windows x86-64, Linux x86-64, macOS Apple Silicon, and macOS Intel.
The workflow can also be started manually from Actions. Pull requests build
packages for testing but cannot publish a release.

Download the archive for your platform and extract the entire directory.
Keep the executable, SDL library, and `assets` directory together.
Check `SHA256SUMS.txt` against the downloaded ZIP and include `COMMIT.txt`
when reporting a bug. Repeated runs of the same commit on the same day update
that day's prerelease; older releases are retained.

Original F-15 Strike Eagle II game files are required and are not included.
The included `assets` directory contains only the repository's replacement art.
From a terminal in the extracted directory, run:

```sh
./f15se2-ex --game /path/to/original/game
```

On Windows, use PowerShell:

```powershell
.\f15se2-ex.exe --game 'C:\Games\F15'
```

Without `--game`, the game uses `F15SE2_DIR` if set, otherwise the current
directory. Linux packages require Ubuntu 24.04 or a compatible newer system,
OpenGL, and the normal window/audio system libraries. Windows requires the
Microsoft Visual C++ x64 Redistributable. macOS packages target macOS 15 or newer.
These builds are unsigned and macOS builds are not notarized; the operating
system may require explicit approval before running a downloaded executable.

Maintainers can reproduce the package staging step with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON -DSDL_SHARED=ON -DSDL_STATIC=OFF
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --component Runtime --prefix stage/f15se2-ex
```
