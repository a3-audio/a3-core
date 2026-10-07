#!/usr/bin/env python3
"""Mute REAPER's gate tracks in a copy of the template (a3-system#74).

The template starts the layout's gate tracks muted -- main, booth, phones and
the main meter's track main_vu; Core opens them after its start-up recall. This
sets the mute -- the first field of a track's MUTESOLO line -- on the gate
tracks that are not muted yet and nothing else, writes the result to a copy
and lists the lines it changed. A template is never edited in place: the copy
is diffed, then put where it belongs by hand.

The gate tracks are read from the layout's gate block, so the template and
Core's gate cannot name different tracks.

Usage:
  reaper_gate_template.py SOURCE DEST
Exit status: 0 when every gate track ends muted and only gate mute fields
changed (none, if all were muted already); 2 on refusal, nothing written.
"""

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LAYOUT = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
          / "share/a3-core/layout.json")
sys.path.insert(0, str(LAYOUT.parents[2] / "lib"))

from a3_core_layout import load_layout   # noqa: E402


def gate_names(layout_path=LAYOUT):
    gate = load_layout(layout_path).gate
    return tuple(sorted(gate, key=gate.get))


GATE_NAMES = gate_names()

#: A track's own lines sit at four spaces inside its <TRACK block; deeper ones
#: belong to plug-ins and envelopes (a track's own lines, as this tool reads them).
_TRACK = "  <TRACK"
_NAME = "    NAME "
_MUTESOLO = re.compile(r"^(    MUTESOLO )(\S+)((?: \S+)*)(\r?\n)?$")


def _track_name(line):
    return line[len(_NAME):].strip().strip('"')


def _gate_mute_lines(lines, names):
    """{index: name} of each gate track's own first MUTESOLO line."""
    found, current = {}, None
    for index, line in enumerate(lines):
        if line.startswith(_TRACK):
            current = None
        elif line.startswith(_NAME):
            current = _track_name(line)
        elif current in names and current not in found.values() \
                and _MUTESOLO.match(line):
            found[index] = current
    return found


def mute_tracks(text, names=GATE_NAMES):
    lines = text.splitlines(keepends=True)
    seen = [_track_name(line) for line in lines if line.startswith(_NAME)]
    for name in names:
        if seen.count(name) != 1:
            raise ValueError(f"track {name!r} appears {seen.count(name)} times, "
                             "not once")
    targets = _gate_mute_lines(lines, names)
    missing = sorted(set(names) - set(targets.values()))
    if missing:
        raise ValueError(f"no MUTESOLO line found for {missing}")
    for index in targets:
        match = _MUTESOLO.match(lines[index])
        lines[index] = f"{match.group(1)}1{match.group(3)}{match.group(4) or ''}"
    return "".join(lines)


def changed_lines(before, after):
    old, new = before.splitlines(), after.splitlines()
    if len(old) != len(new):
        raise ValueError("the line count changed")
    return [(number, a, b) for number, (a, b) in enumerate(zip(old, new), 1)
            if a != b]


def stray_changes(before, after, names=GATE_NAMES):
    """Changed lines that are not a gate track's mute field going 0 -> 1."""
    targets = _gate_mute_lines(before.splitlines(), names)
    strays = []
    for number, old, new in changed_lines(before, after):
        old_match, new_match = _MUTESOLO.match(old), _MUTESOLO.match(new)
        is_gate_mute = (number - 1 in targets and old_match and new_match
                        and old_match.group(3) == new_match.group(3)
                        and new_match.group(2) == "1")
        if not is_gate_mute:
            strays.append((number, old, new))
    return strays


def _refuse(message):
    print(f"refused: {message}", file=sys.stderr)
    return 2


def main(argv, names=None):
    names = GATE_NAMES if names is None else tuple(names)
    if len(argv) != 2:
        print(__doc__)
        return 2
    source, dest = Path(argv[0]), Path(argv[1])
    if dest.exists() and os.path.samefile(source, dest) \
            or dest.resolve() == source.resolve():
        print("refused: DEST is SOURCE -- the template is never edited in place")
        return 2
    # Bytes, not read_text: text mode would translate line ends, and
    # surrogateescape keeps any non-UTF-8 byte of a plug-in's state as it was.
    before = source.read_bytes().decode("utf-8", "surrogateescape")
    try:
        after = mute_tracks(before, names)
        strays = stray_changes(before, after, names)
    except ValueError as refusal:
        return _refuse(refusal)
    if strays:
        for number, old, new in strays:
            print(f"{number}: {old.strip()}  ->  {new.strip()}", file=sys.stderr)
        return _refuse(f"{len(strays)} lines outside the gate mutes would change")
    changes = changed_lines(before, after)
    targets = _gate_mute_lines(before.splitlines(), names)
    for number, old, new in changes:
        print(f"{number}: {old.strip()}  ->  {new.strip()}")
    dest.write_bytes(after.encode("utf-8", "surrogateescape"))
    muted = ", ".join(targets[number - 1] for number, _, _ in changes)
    print(f"{len(changes)} lines changed ({muted or 'all gate tracks were muted'}); "
          f"{dest} written")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
