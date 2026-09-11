# Quality checks

Automated checks give reviewers evidence; the contributor policy still requires
reading and understanding the change. A green run does not prove correct gameplay.

| Check | When | Result |
| --- | --- | --- |
| Behavior tests, including golden images | Every PR; Linux, Windows, macOS | Required job succeeds only when tests pass |
| ASan and UBSan | Every PR; Linux | Memory/undefined-behavior failures fail the job |
| Coverage | Every PR; Linux | Codecov plus downloadable XML and text; informational under current policy |
| Cppcheck and Clang static analyzer | Every PR; Linux | Findings for review; analyzer execution errors fail the job |
| Function complexity and dependency graph | Every PR; Linux | Downloadable review metrics, no arbitrary legacy-wide threshold |
| Mutation testing | Manual, selected test target | Baseline must pass; surviving mutations are reported for review |
| Interactive acceptance | Before merging affected gameplay or UI | Contributor records the checks below |

Repository branch protection must be configured by a maintainer to require CI
jobs. Workflows alone cannot enforce merge policy. Static analysis has existing
findings; review new findings and triage existing ones before adopting a blocking
baseline. No blanket suppression of legacy files is applied.

## Local checks

```sh
cmake -S . -B build
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --timeout 120 --no-tests=error

cmake -S . -B build-sanitizers -DASAN=ON -DUBSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitizers --parallel 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-sanitizers --output-on-failure --timeout 120 --no-tests=error

cmake -S . -B build-coverage -DCOVERAGE=ON
cmake --build build-coverage --parallel 2
cmake --build build-coverage --target coverage
```

For static analysis, install cppcheck and `clang-tools-18`. The game's `.c`
sources are C++; the tools use the generated compilation database and game
include paths. Third-party translation units and tests are excluded, but game
sources compiled by isolation tests remain included with their test definitions.

```sh
python3 tools/quality/static_analysis.py build
python3 tools/quality/static_analysis.py build --tool clang
```

Cppcheck returns a failure for findings locally. `--report-only` matches CI's
advisory policy; malformed output and analyzer failures still fail. Clang writes
its findings to `build/quality/clang.txt`. Its exit code checks successful analysis,
not the absence of warnings. Reports apply only to the configured platform and
preprocessor branches. Check the other platform builds before drawing conclusions.

Install `lizard==1.17.31` in a Python virtual environment to collect function
complexity, length, and parameter counts:

```sh
mkdir -p build/quality
lizard --csv src > build/quality/complexity.csv
cmake --graphviz=build/quality/dependencies.dot -S . -B build
```

Use the CSV to spot functions made harder to follow by a change. The DOT graph
shows CMake target dependencies, including third-party libraries; it is not a
function call graph or proof that runtime dependencies are unchanged. Compare
reports produced with the same tools on the base and PR commits when needed.

## Mutation testing

In Actions, run **Mutation tests** and select a test executable. The workflow
pins Mull 0.34.0 with matching Clang 18. It changes arithmetic and comparisons
in compiled game code, then measures whether the selected test detects each
change. It does not edit working source files.

The default signed-ratio suite keeps the initial run small. Utility and software
rasterizer suites are also selectable. This is not a whole-project mutation score:
only code linked into and exercised by that test is measured. Inspect the JSON
report and log in the artifact for killed, surviving, timed-out, or uncovered
mutations. A surviving mutation may identify a missing assertion or an equivalent
change. Investigate it before adding a test. A timeout is not evidence that an
assertion detected an error. Zero discovered mutants is treated as a failed run.

For a local run, install the matching tools using the
[Mull installation guide](https://mull.readthedocs.io/en/latest/Installation.html):

```sh
export MULL_CONFIG="$PWD/tools/quality/mull.yml"
cmake -S . -B build-mutation -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fpass-plugin=/usr/lib/mull-ir-frontend-18 -grecord-command-line -fprofile-instr-generate -fcoverage-mapping'
cmake --build build-mutation --target signed_ratio_behavior_tests --parallel 2
ctest --test-dir build-mutation -R '^signed_ratio_behavior_tests$' --output-on-failure
mull-runner-18 --allow-surviving --workers 2 --timeout 10000 \
  --reporters Elements --reporters IDE --report-dir build-mutation \
  build-mutation/signed_ratio_behavior_tests
```

## Acceptance and reviewer spot checks

For changes affecting gameplay, run with your own original game files. Record
the commit, OS, renderer, settings, inputs, expected behavior, and observed result.
Use a disposable copy of the game directory because pilot records can change.

- Start from the intro, choose a pilot and mission, enter flight, then return
  through debriefing and start a second mission.
- For rendering changes, check both software and OpenGL: cockpit, external views,
  HUD, palette effects, and title/menu transitions. Compare an affected scene
  before and after; do not update golden images merely to make tests pass.
- For input changes, check keyboard and the affected mouse/joystick controls,
  including pause/resume and releasing a held control.
- For sound changes, listen to intro music, engine/effects, and a speech cue.
- Exercise the bug's original trigger and a nearby boundary case. Attach a small
  reproducer or screenshot when it explains the change better than prose.

Reviewers should inspect changed public interfaces, ownership/lifetimes, array
bounds, original DOS integer-width semantics, and any changes to tests or goldens.
Check that assertions observe behavior rather than duplicating the implementation.
