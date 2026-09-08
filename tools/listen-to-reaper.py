#!/usr/bin/env python3
"""Listen to what REAPER sends, and say what it is.

**Why this exists before any relaying does.** The spec wants Core to follow
REAPER's values instead of asserting its own. Nobody here knows what REAPER
actually sends of its own accord -- the control surface has been pointed at
192.168.43.96 since before anyone remembers, a test laptop that is long gone,
so the return path has never been observed. Guessing at it and writing the
relay first would be building on a story.

So this listens and prints, and nothing else. It is not part of a3-core.py and
starts no server that anything depends on: run it, point REAPER at it, turn a
knob, read what comes back.

    ~/.venv/bin/python3 tools/listen-to-reaper.py --port 9002

Then in REAPER: Preferences -> Control/OSC/web -> the OSC device, set the
outgoing host to 127.0.0.1 and the port to the same. That change is the
maintainer's to make in REAPER's own dialog rather than by editing reaper.ini,
because the csurf_0 line carries the *incoming* path in the same string and
that one works today.

Addresses are named against layout.json where they can be, so a line reads as
"channel 2's input gain" rather than "/track/20/fx/1/fxparam/1/value".
"""

import argparse
import sys
import time
from collections import Counter
from pathlib import Path

PACKAGE = (Path(__file__).resolve().parents[1]
           / "platform-config/debian-x86_64/a3-core/home/aaa/.local")
sys.path.insert(0, str(PACKAGE / "lib"))

from a3_core_layout import load_layout                      # noqa: E402
from pythonosc import dispatcher as osc_dispatcher          # noqa: E402
from pythonosc import osc_server                            # noqa: E402


def describe(layout, address):
    """The address in A3's own words, where the layout can supply them."""
    parts = address.strip("/").split("/")
    if len(parts) < 2 or parts[0] != "track":
        return ""

    try:
        track = int(parts[1])
    except ValueError:
        return ""

    role = layout.track_role(track)
    if role is None:
        master = {getattr(layout.master, f): f
                  for f in ("track_masterbus", "track_booth", "track_phones",
                            "track_ph_mix", "aux_return")}
        return f"master.{master[track]}" if track in master else ""

    channel, field = role
    return f"channel {channel}.{field}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9002)
    parser.add_argument("--layout", default=str(PACKAGE / "share/a3-core/layout.json"))
    parser.add_argument("--quiet", action="store_true",
                        help="count instead of printing every message; a "
                             "moved fader is hundreds of them")
    args = parser.parse_args()

    layout = load_layout(args.layout)
    seen = Counter()

    started = time.monotonic()

    def heard(address, *values):
        seen[address] += 1
        if args.quiet:
            return
        # Seconds since this started, to a thousandth. Without it a burst of
        # three thousand messages and a slow trickle look identical in a log,
        # and telling those apart is the whole question: does REAPER report
        # every change, or only dump everything when the surface reconnects?
        when = time.monotonic() - started
        named = describe(layout, address)
        shown = " ".join(f"{v:.4f}" if isinstance(v, float) else str(v)
                         for v in values)
        # Flushed: this is watched live while somebody turns a knob, and
        # Python buffers stdout when it is not a terminal -- piped to a file
        # or through tee, every line would sit in the buffer until the process
        # ended, which for a listener is until you gave up on it.
        print(f"{when:9.3f}  {address:52} {shown:>12}  {named}",
              flush=True)

    dispatcher = osc_dispatcher.Dispatcher()
    dispatcher.set_default_handler(heard)

    server = osc_server.ThreadingOSCUDPServer((args.ip, args.port), dispatcher)
    print(f"listening on {args.ip}:{args.port} -- point REAPER's OSC output "
          f"here and turn something\n", file=sys.stderr)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print(f"\n{sum(seen.values())} messages, {len(seen)} distinct "
              f"addresses:", file=sys.stderr)
        for address, count in seen.most_common():
            print(f"  {count:6}  {address:52} {describe(layout, address)}",
                  file=sys.stderr)


if __name__ == "__main__":
    main()
