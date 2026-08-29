# Parameter curves — a safety net before the port

The curves in `a3-core.py` (`slope_constant_power`, `slope_eq`,
`slope_crossfade_gain` and the rest) turn a controller value into a DSP
setting. They are pure functions of one number and hold no state, which makes
them the one part of a3-core that can be rewritten in another language with
certainty rather than hope.

`curves-golden.json` is that certainty: what each curve answers at every
hundredth from 0.00 to 1.00, recorded from the Python that is running today.

## Use

```bash
./check_curves.py            # compare the current curves against the record
./check_curves.py --update   # record the current behaviour as the new reference
```

`--update` only when a curve was *meant* to change — otherwise the net is gone,
and the commit should say what changed and why.

## When the port happens

A C++ implementation can be held against the same file: read it, call the new
curve at each x, compare. Same numbers, same tolerance (1e-9). That turns "it
sounds about right" into a check that either passes or names the input where it
does not.

## Why it runs without numpy

`a3-core.py` imports numpy, but the curves themselves use only seven names from
it — `interp`, `clip`, `arange`, `sin`, `cos`, `log10`, `pi`. `numpy_subset.py`
supplies those, so the check runs on any machine in the system rather than only
on the Core. The functions are lifted out of the syntax tree rather than
imported, so nothing else in `a3-core.py` runs: importing it would open sockets
and start a server.
