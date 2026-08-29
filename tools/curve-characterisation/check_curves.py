#!/usr/bin/env python3
"""Check that the parameter curves in a3-core.py still do what they did.

Why this exists
---------------
The curves — `slope_constant_power`, `slope_eq`, `slope_crossfade_gain` and the
rest — are what turns a controller value into a DSP setting. They are pure
functions of one number and carry no state, which makes them the one part of
a3-core that can be rewritten in another language with certainty rather than
hope. This file is the certainty: it records what each curve answers across its
whole input range, so a reimplementation can be held against the original
instead of against somebody's memory of it.

Run it before touching a curve, and again afterwards. A difference it reports is
a difference a listener would hear.

Usage
-----
    ./check_curves.py                 # compare against curves-golden.json
    ./check_curves.py --update        # record the current behaviour instead

`--update` rewrites the golden file. Only do that when a curve was *meant* to
change, and say so in the commit — otherwise the net is gone.

Notes
-----
Runs without numpy. a3-core.py imports it, but the curves themselves need only
seven names from it, which `numpy_subset.py` provides; that way the check runs
on any machine in the system, not only on the Core.
"""

import argparse
import ast
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
GOLDEN = HERE / "curves-golden.json"
SOURCE = (HERE / "../../platform-config/debian-x86_64/a3-core"
          "/home/aaa/.local/bin/a3-core.py").resolve()

SAMPLES = 101  # 0.00 to 1.00 in hundredths
TOLERANCE = 1e-9


def load_curves(source):
    """The curve functions alone, lifted out of a3-core.py.

    Importing the module would open sockets and start a server; the curves are
    taken out of the syntax tree instead, so nothing else in the file runs.
    """
    sys.path.insert(0, str(HERE))
    import numpy_subset

    tree = ast.parse(source.read_text())
    curves = [n for n in tree.body
              if isinstance(n, ast.FunctionDef) and n.name.startswith("slope_")]
    if not curves:
        raise SystemExit(f"no slope_* functions found in {source}")

    module = ast.Module(body=curves, type_ignores=[])
    namespace = {"np": numpy_subset}
    exec(compile(ast.fix_missing_locations(module), "<curves>", "exec"), namespace)

    return {name: fn for name, fn in namespace.items() if name.startswith("slope_")}


def measure(curves):
    table = {}
    for name in sorted(curves):
        rows = []
        for i in range(SAMPLES):
            x = round(i / (SAMPLES - 1), 2)
            try:
                y = curves[name](x)
                y = ([round(float(v), 9) for v in y]
                     if isinstance(y, (tuple, list)) else round(float(y), 9))
            except Exception as error:                      # noqa: BLE001
                y = f"ERROR {type(error).__name__}: {error}"
            rows.append([x, y])
        table[name] = rows
    return table


def differences(now, before):
    """Every place the two disagree, in the order a reader wants them."""
    out = []

    for name in sorted(set(before) - set(now)):
        out.append(f"{name}: gone")
    for name in sorted(set(now) - set(before)):
        out.append(f"{name}: new — record it with --update")

    for name in sorted(set(now) & set(before)):
        for (x, a), (_, b) in zip(now[name], before[name]):
            if isinstance(a, list) and isinstance(b, list):
                same = len(a) == len(b) and all(
                    abs(p - q) <= TOLERANCE for p, q in zip(a, b))
            elif isinstance(a, float) and isinstance(b, float):
                same = abs(a - b) <= TOLERANCE
            else:
                same = a == b
            if not same:
                out.append(f"{name}({x}): {b} -> {a}")

    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update", action="store_true",
                        help="record the current behaviour as the new reference")
    args = parser.parse_args()

    table = measure(load_curves(SOURCE))

    if args.update or not GOLDEN.exists():
        GOLDEN.write_text(json.dumps(table, indent=1) + "\n")
        print(f"recorded {len(table)} curves x {SAMPLES} points -> {GOLDEN.name}")
        return 0

    found = differences(table, json.loads(GOLDEN.read_text()))
    if not found:
        print(f"{len(table)} curves unchanged across {SAMPLES} points each")
        return 0

    print(f"{len(found)} difference(s):")
    for line in found[:40]:
        print("  " + line)
    if len(found) > 40:
        print(f"  … and {len(found) - 40} more")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
