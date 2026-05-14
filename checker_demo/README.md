# checker_demo

Small consumer project for `../checker`.

Algorithm: given `N` integers, print their sum.

## Build

```bash
cmake -S checker_demo -B checker_demo/build
cmake --build checker_demo/build --target demo
```

## Try It

GUI:

```bash
checker_demo/build/demo
checker_demo/build/demo build
checker_demo/build/demo cases
```

Headless:

```bash
checker_demo/build/demo --headless build
checker_demo/build/demo --headless cases
```

Force a failure and run saved cases:

```bash
CHECKER_DEMO_FORCE_FAIL=1 checker_demo/build/demo --headless cases
```

The program entry point is `src/main.cpp`. The GUI shows the same workflow with
Tests, Invocations, and Files tabs. The Detail modal's Text tab compares the
reference sum and target sum. `tests/cases` can mix input-only generator outputs
and manual `[input]` / `[output]` expected-output files.
