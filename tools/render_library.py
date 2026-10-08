#!/usr/bin/env python3
"""a3-doc's clip table, rendered from the clips Motion ships.

The library page's clip table was written by hand from the library plan. Each
clip carries its own mood now, so this writes the table from Motion's clip
files between markers, the way render_docs.py writes the OSC tables, and the
prose around it stays the writers'.

  <!-- a3-motion:clips -->    phase, clip, shape, mood; a phase's spare is
  <!-- /a3-motion:clips -->   the clip its set leaves out

Usage: render_library.py [A3_MOTION_UI_CHECKOUT [A3_DOC_CHECKOUT]]
       (default: both beside a3-core in the a3-system checkout)
"""

import json
import re
import sys
from pathlib import Path

#: The page with the table, relative to an a3-doc checkout.
PAGE = "src/user/a3motion-library.md"
#: Where Motion keeps the shipped clips and sets, relative to its checkout.
CLIPS = "pattern/clips/system"
SETS = "pattern/sessions/system"

#: The phases in the order of a night, then the mood sets. A clip's phase is
#: the first word of its name; a phase missing here is refused rather than
#: sorted somewhere, so a new set is placed in the table on purpose.
PHASES = ("Warmup", "Groove", "Build", "Peak", "Drop", "Break", "Dub", "Deep",
          "Float", "Closing", "Tribal", "Tension", "Acid", "Ambient", "Space")

BEGIN, END = "<!-- a3-motion:clips -->", "<!-- /a3-motion:clips -->"


def _row(cells):
    return "| " + " | ".join(str(c).replace("|", "\\|") for c in cells) + " |"


def _phase_and_short_name(name):
    phase, _, short = name.partition(" ")
    if phase not in PHASES:
        raise ValueError(f"clip {name!r}: phase {phase!r} is not in PHASES")
    return phase, short


def _set_positions(sets):
    """Each clip a set plays, by its channel: the order a DJ meets them in."""
    positions = {}
    for a_set in sets:
        for channel_index, channel in enumerate(a_set.get("channels", [])):
            for slot in channel.get("slots", []):
                positions.setdefault(slot.get("clip"), channel_index)
    return positions


def clips_table(clips, sets):
    """One row a clip with a shape, phases in PHASES' order, each phase named
    on its first row only; within a phase the set's clips by channel, then
    the spare."""
    positions = _set_positions(sets)
    set_names = {a_set.get("name") for a_set in sets}
    by_phase = {}
    for clip in clips:
        if not clip.get("svg"):
            continue
        phase, short = _phase_and_short_name(clip["name"])
        by_phase.setdefault(phase, []).append((short, clip))

    def order(entry):
        short, clip = entry
        return (clip["name"] not in positions, positions.get(clip["name"], 0), short)

    rows = [_row(["Phase", "Clip", "Shape", "Mood"]), _row(["---"] * 4)]
    for phase in PHASES:
        for i, (short, clip) in enumerate(sorted(by_phase.get(phase, []), key=order)):
            spare = phase in set_names and clip["name"] not in positions
            rows.append(_row([f"**{phase}**" if i == 0 else "",
                              f"{short} (spare)" if spare else short,
                              clip["svg"], clip.get("mood") or "–"]))
    return "\n".join(rows)


def put_table(text, clips, sets):
    """`text` with the marked table replaced by its render; nothing else
    changes, and a page without markers comes back as it was."""
    block = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END), re.DOTALL)
    return block.sub(lambda _m: f"{BEGIN}\n{clips_table(clips, sets)}\n{END}", text)


def load(motion):
    """The shipped clips and sets of the Motion checkout `motion`."""
    def read(folder):
        return [json.loads(p.read_text()) for p in sorted((motion / folder).glob("*.json"))]
    return read(CLIPS), read(SETS)


def default_motion(root):
    """Motion's UI beside the a3-core checkout `root`, as the umbrella holds it."""
    return root.parent / "a3-motion" / "ui"


def default_doc(root):
    """a3-doc beside the a3-core checkout `root`."""
    return root.parent / "a3-doc"


def main(argv):
    root = Path(__file__).resolve().parents[1]
    motion = Path(argv[0]) if argv else default_motion(root)
    doc = Path(argv[1]) if len(argv) > 1 else default_doc(root)
    clips, sets = load(motion)
    path = doc / PAGE
    before = path.read_text()
    after = put_table(before, clips, sets)
    if after != before:
        path.write_text(after)
        print(f"rendered {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
