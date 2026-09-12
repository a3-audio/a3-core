"""Is the tap cheap enough to sit on every message?

That is the question, and it is not the same as "is it cheaper than print()".
`seen()` runs on every OSC message -- about a hundred a second on this rig,
and 301,385 in an hour on one address alone -- so what matters is the absolute
cost per call against that rate, not a ratio against whatever it replaces.

Two sinks are measured for print(), because they answer different things.
/dev/null is the floor: nothing reads it and the kernel drops the write, so it
is the cheapest a print() could ever be. A drained pipe is the fair stand-in
for journald: a real descriptor with a real reader copying bytes out of it.
The truth about systemd is somewhere at or above the pipe, since journald also
timestamps, rate-limits and writes to disk.

Run: ~/.venv/bin/python3 tools/bench-traffic.py
"""

import os
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core"
                       / "home/aaa/.local/lib"))

from a3_core_traffic import IN, Traffic   # noqa: E402

ROUNDS = 200000
ADDRESS = "/channel/0/azimuth"


def time_it(label, work):
    start = time.perf_counter()
    work()
    elapsed = time.perf_counter() - start
    per_call_us = elapsed / ROUNDS * 1e6
    print(f"{label:24s} {elapsed:7.3f} s total   {per_call_us:7.3f} us/call   "
          f"{ROUNDS / elapsed:10.0f} /s")
    return per_call_us


def main():
    traffic = Traffic()

    def tap():
        for i in range(ROUNDS):
            traffic.seen(IN, ADDRESS, i * 0.001, "motion")

    # Not a terminal: that would measure the terminal. /dev/null is the
    # floor and the drained pipe is the honest comparison -- see the module
    # docstring for why both are here.
    devnull = open(os.devnull, "w")

    def printing():
        for i in range(ROUNDS):
            print(ADDRESS + " : " + str(i * 0.001), file=devnull)

    print(f"{ROUNDS} rounds each\n")
    tap_us = time_it("traffic.seen()", tap)
    print_us = time_it("print() to /dev/null", printing)
    devnull.close()

    # A pipe with someone reading it: a real descriptor, real copying, a real
    # reader. That is what journald is, minus its timestamping, rate limiting
    # and disk writes -- so Core's true print() cost sits at or above this.
    read_fd, write_fd = os.pipe()
    pipe_out = os.fdopen(write_fd, "w")

    def drain():
        with os.fdopen(read_fd, "rb") as tap_end:
            while tap_end.read(65536):
                pass

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()

    def printing_to_pipe():
        for i in range(ROUNDS):
            print(ADDRESS + " : " + str(i * 0.001), file=pipe_out)

    pipe_us = time_it("print() to a drained pipe", printing_to_pipe)
    pipe_out.close()
    reader.join(timeout=5)

    print()
    # The verdict is absolute, not comparative. At this rig's rate the tap's
    # whole cost is a rounding error, and that holds whichever sink print()
    # would have gone to.
    print(f"At ~100 messages a second the tap costs "
          f"{tap_us * 100 / 1000:.3f} ms per second, "
          f"{tap_us / 100:.4f}% of one core.")
    print(f"print() floor (/dev/null): {print_us:.3f} us   "
          f"drained pipe: {pipe_us:.3f} us")
    if tap_us > 20.0:
        print("WARNING: over 20 us per call is no longer a rounding error at "
              "a hundred a second. Report this rather than carrying on.")


if __name__ == "__main__":
    main()
