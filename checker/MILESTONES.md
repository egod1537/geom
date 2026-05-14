# checker Milestones

The public spec may call the library `geom_checker`; this monorepo implements
the package as `checker`.

```text
Track L: L1 API -> L2 Script parser -> L3 Headless core -> L4 GUI -> L5 CMake + demo
Track C: C1 Skeleton -> C2 Envelope -> C3 Gate
```

Track L is one-time library work. Track C repeats per algorithm under
`~/proj/geom/<algo>/tests/`.

## L1 API Skeleton

Deliverables:

- `include/checker/config.h` with `StressConfig` and `GuiConfig`
- `include/checker/{result,script,stress,dispatch,gui}.h`
- placeholder `src/**/*.cpp`
- vendored `shared/testlib.h`
- vendored `third_party/imgui/`
- `checker`, `checker::imgui`, and `checker::testlib` CMake targets

Checks:

- `cmake -S checker -B checker/build`
- `cmake --build checker/build`
- `libchecker.a` exists
- scratch compile with `#include <checker/stress.h>`
- vendored files record commit hashes

## L2 Script Parser

Deliverables:

- `ScriptEntry`, `Script`, `RangeArg`, and `ArgToken`
- `parse_script()`
- `parse_script_string()`
- `resolve_args()`
- unit tests for empty input, comments, `case`, `stress`, positive/negative
  ranges, malformed input, duplicate names, and deterministic resolution

Checks:

- `ctest --test-dir checker/build`
- parser errors include line numbers
- parser has no subprocess or GUI code

## L3 Headless Core + Dispatcher

Deliverables:

- POSIX generator subprocess execution with timeout
- case IO, generated case writing, failed archive writing
- `run_all_stress()`
- `run_one_stress()`
- `build_cases()`
- `run_cases()`
- `run(cfg, argc, argv)` with `--headless`

Checks:

- target equals reference gives `fail=0`
- target differs from reference obeys `stop_on_first_fail` and `max_fail`
- normalize is applied before compare
- failed archives are created under `failed/`
- `run_cases()` skips `failed/`
- `build_cases()` writes script `case` outputs
- subprocess timeout is tested
- dispatcher handles `--headless stress`, `stress <name>`, `build`, `cases`,
  `--help`, and invalid arguments

## L4 GUI

Deliverables:

- GLFW + OpenGL3 + ImGui initialization and cleanup
- Stress, Build, and Cases tabs
- incremental stress execution from the GUI
- failed-case inspect list and Case Detail modal
- `checker::gui::Canvas2D`
- Text tab via `gui.format_output`
- optional Plot tab via `gui.render_plot_2d`

Checks:

- default `./prog` opens a window
- Stress tab Run/Pause/Stop works
- Detail modal always has Text
- Detail Plot tab appears only when `render_plot_2d` is set
- Build tab rebuilds scripted cases
- Cases tab can inspect saved `.txt` cases
- `--headless` never opens a window

## L5 CMake Helper + Self-Test

Deliverables:

- `cmake/checker_helpers.cmake`
- `cmake/checker-config.cmake.in`
- install/export rules
- `checker_add_stress_test()`
- `examples/sort_uniq/` with `tests/gen.script` and GUI rendering
- `CHECKER_BUILD_EXAMPLES` option
- complete README

Checks:

- `cmake --install` works
- scratch project can `find_package(checker REQUIRED)`
- helper builds generator and stress binaries
- `gen_paths.h` contains absolute generator/script/cases paths
- parallel build dependencies are correct
- `sort_uniq_stress` opens the GUI
- `sort_uniq_stress --headless build` writes scripted cases
- `sort_uniq_stress --headless cases` returns `fail=0`
- `sort_uniq_stress --headless stress small` returns `fail=0`

## C1 Consumer Skeleton

Scope: `~/proj/geom/<algo>/tests/` only.

Deliverables:

- `tests/gen/gen_<flavor>.cpp`
- `tests/gen.script`
- `tests/stress_test.cpp` with trivial callbacks and optional `render_plot_2d`
- `tests/cases/failed/.gitkeep`
- `tests/cases/README.md`
- `tests/CMakeLists.txt`

Checks:

- `<algo>_stress` builds
- `<algo>_stress` opens the GUI
- `<algo>_stress --headless stress` exits successfully with trivial callbacks
- `<algo>_stress --headless build` creates the smoke scripted case
- `<algo>_stress --headless cases` works
- `<algo>_stress --headless stress smoke` works

## C2 Consumer Envelope

Deliverables:

- real testlib generator
- real parser
- canonical normalizer
- brute reference
- optional `render_plot_2d` for small-case 2D diagnosis
- actual `gen.script` entries
- frozen hand-written cases
- complete cases README

Checks:

- scripted cases build
- frozen cases are processed
- generator standalone invocation works
- broken input makes `parse_input` throw
- normalize absorbs order/duplicate differences
- reference is simple and independent of target helpers
- GUI Detail Text shows formatted output
- optional GUI Detail Plot shows meaningful input/reference/target state

## C3 Consumer Gate

Deliverables:

- `target` callback calls the algorithm library
- test CMake links the algorithm target
- useful failed cases are promoted and documented

Checks:

- body stub fails on non-empty reference answers
- headless `cases`, `build`, and named `stress` modes work
- GUI mode shows the same cases and failures
- temporary empty target detects differences on stress
- temporary `target = reference` has `fail=0`
- temporary sanity edits are reverted before review
