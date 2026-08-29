"""Just enough numpy for the parameter curves in a3-core.py.

The curves use seven numpy names between them — interp, clip, arange, sin, cos,
log10 and pi — and nothing that needs an array library. This shim exists so the
curves can be captured on a machine that has no numpy, which is every machine in
this system except the Core itself.
"""
import math

pi = math.pi


def arange(start=0, stop=1, step=1):
    # numpy's arange walks start + i*step, and stops before stop. Reproducing
    # the multiplication rather than accumulating keeps the same rounding.
    out, i = [], 0
    while True:
        v = start + i * step
        if (step > 0 and v >= stop) or (step < 0 and v <= stop):
            return out
        out.append(v)
        i += 1


def interp(x, xp, fp):
    # Linear interpolation, clamped at both ends — the behaviour that decides
    # what a curve does above its last support point.
    if x <= xp[0]:
        return float(fp[0])
    if x >= xp[-1]:
        return float(fp[-1])
    for i in range(1, len(xp)):
        if x <= xp[i]:
            span = xp[i] - xp[i - 1]
            t = 0.0 if span == 0 else (x - xp[i - 1]) / span
            return float(fp[i - 1] + t * (fp[i] - fp[i - 1]))
    return float(fp[-1])


def clip(v, lo, hi):
    return max(lo, min(hi, v))


def sin(x):
    return math.sin(x)


def cos(x):
    return math.cos(x)


def log10(x):
    return math.log10(x)
