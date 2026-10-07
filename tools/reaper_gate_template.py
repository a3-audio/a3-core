#!/usr/bin/env python3
"""Mute REAPER's outputs in a copy of the template (a3-system#74).

The template starts main, booth and phones muted; Core opens them after its
start-up recall. This sets the mute -- the first field of a track's MUTESOLO
line -- on the tracks with those names and nothing else, writes the result to
a copy and lists the lines it changed. A template is never edited in place:
the copy is diffed, then put where it belongs by hand.

Usage:
  reaper_gate_template.py SOURCE DEST
Exit status: 0 when exactly three lines changed, 1 otherwise, 2 on refusal.
"""

import re
import sys
from pathlib import Path

GATE_NAMES = ("main", "booth", "phones")

#: A track's own lines sit at four spaces inside its <TRACK block; deeper ones
#: belong to plug-ins and envelopes (as in test_layout_against_reaper).
_TRACK = "  <TRACK"
_NAME = "    NAME "
_MUTESOLO = re.compile(r"^(    MUTESOLO )\S+((?: \S+)*)(\r?\n)?$")


def _track_name(line):
    return line[len(_NAME):].strip().strip('"')


def mute_tracks(text, names=GATE_NAMES):
    lines = text.splitlines(keepends=True)
    seen = [_track_name(line) for line in lines if line.startswith(_NAME)]
    for name in names:
        if seen.count(name) != 1:
            raise ValueError(f"track {name!r} appears {seen.count(name)} times, "
                             "not once")
    current, done, out = None, set(), []
    for line in lines:
        if line.startswith(_TRACK):
            current = None
        elif line.startswith(_NAME):
            current = _track_name(line)
        match = _MUTESOLO.match(line)
        if match and current in names and current not in done:
            line = f"{match.group(1)}1{match.group(2)}{match.group(3) or ''}"
            done.add(current)
        out.append(line)
    missing = sorted(set(names) - done)
    if missing:
        raise ValueError(f"no MUTESOLO line found for {missing}")
    return "".join(out)


def changed_lines(before, after):
    old, new = before.splitlines(), after.splitlines()
    if len(old) != len(new):
        raise ValueError("the line count changed")
    return [(number, a, b) for number, (a, b) in enumerate(zip(old, new), 1)
            if a != b]


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    source, dest = Path(argv[0]), Path(argv[1])
    if dest.resolve() == source.resolve():
        print("refused: DEST is SOURCE -- the template is never edited in place")
        return 2
    # Bytes, not read_text: text mode would translate line ends, and
    # surrogateescape keeps any non-UTF-8 byte of a plug-in's state as it was.
    before = source.read_bytes().decode("utf-8", "surrogateescape")
    after = mute_tracks(before)
    dest.write_bytes(after.encode("utf-8", "surrogateescape"))
    changes = changed_lines(before, after)
    for number, old, new in changes:
        print(f"{number}: {old.strip()}  ->  {new.strip()}")
    print(f"{len(changes)} lines changed; {dest} written")
    return 0 if len(changes) == len(GATE_NAMES) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
