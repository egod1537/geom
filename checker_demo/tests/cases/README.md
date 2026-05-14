# demo_sum Cases

Input:

```text
N
a1 a2 ... aN
```

The target returns the sum of the `N` integers.

Canonical output is an in-process `int64_t`.
The Text tab formats it as one integer.

## Frozen Cases

- `001_basic.txt`: mixed positive and negative values
- `002_zero.txt`: empty list, answer is `0`
- `003_manual_expected.txt`: stores both manual input and expected output

## Scripted Cases

Run `./demo --headless build` to generate:

- `tiny.txt`
- `mixed.txt`
- `large.txt` with `N = 200000`

Manual expected-output cases use:

```text
[input]
...
[output]
...
```

Input-only `.txt` files are still valid and use the in-process reference answer.
