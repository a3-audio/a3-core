"""The catalogue of addresses the system can speak, held against the traffic.

`a3_core_traffic` counts what flew past. It cannot say what is *possible*: an
address nobody has sent has no row there at all, which is why looking up a
channel bus's FX send in the window gave three rows and none of them the
answer. The catalogue is the other half, generated from the six sources by
tools/osc_register.py and shipped as share/a3-core/osc-register.json.

Put the two together and the window can say the thing that cost three evenings
this week: **an address that is in the catalogue and has never arrived is a
dead wire.** Motion's mixer talking to the beat-analyzer for two days, the pots
with no way back, the position that never came -- all three look like this.

**Why the matching is here and not in the page.** It is arithmetic over two
lists, and there is no JavaScript engine on this machine to test arithmetic
written in the page with. The page draws what this says.

**The same rule as a3_core_state.StateFile:** a register that is missing,
half-written or of a shape this version does not know is an empty register with
a problem attached, never an exception. Core makes the sound; this is a
convenience, and a convenience that stops the device coming up is worse than
no convenience at all.
"""

import json
import re
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List

#: Where the register lives when nobody says otherwise: beside layout.json in
#: the package, because the installed Core has no repos beside it to rebuild
#: from -- see tools/osc_register.py.
DEFAULT_REGISTER = (Path(__file__).resolve().parent.parent
                    / "share/a3-core/osc-register.json")

#: What an entry carries. An older or newer file may be short of one, and a
#: missing field is filled in with a blank rather than taking the whole
#: register down with it -- same reasoning as apply_state().
FIELDS = ("address", "device", "direction", "source", "note")

#: A segment that is only a number. REAPER numbers its tracks, its FX slots
#: and its parameters, which is how one project comes to report 19,335
#: addresses that are really a few dozen shapes.
_NUMBERED = re.compile(r"(?<=/)\d+(?=/|$)")

#: A placeholder in a template: `{ch}` as this system writes it, `@` as
#: REAPER's pattern file does.
_PLACEHOLDER = re.compile(r"\{[^}]*\}|@")


def collapse(address: str) -> str:
    """One address standing for every numbering of itself.

    `/track/17/volume` and `/track/3/volume` are the same wire with a
    different track on it. Collapsing the number is what makes matching the
    catalogue against the traffic affordable: without it, 518 templates
    against 19,335 REAPER addresses is ten million regular expressions on a
    machine that is making sound.

    Only a segment that is *entirely* a number collapses.
    `/MultiEncoder/azimuth0` keeps its 0, because `azimuth0` is a word -- the
    pattern side handles that one instead, see matcher().
    """
    return _NUMBERED.sub("0", address)


def counts_of(rows: Iterable[Dict[str, Any]]) -> Dict[str, int]:
    """How many messages each collapsed address has carried.

    Counts are added up across the numbering, so the catalogue's
    `/track/@/volume` reports what every track's volume carried together.
    That is the honest reading of one row standing for many.
    """
    counts: Dict[str, int] = {}
    for row in rows:
        key = collapse(row["address"])
        counts[key] = counts.get(key, 0) + row["count"]
    return counts


def matcher(template: str) -> Callable[[str], bool]:
    """Whether an address is an instance of this template.

    Three kinds of placeholder, and they do not mean the same thing:

    * `{ch}` and REAPER's `@` stand for one value inside one segment -- a
      channel number, a track number, a parameter number. They must not reach
      across a `/`, or `/track/@/volume` would match
      `/track/0/fx/1/volume` and the register would claim a wire is alive
      because a different one is.
    * `*` is OSC's own wildcard, and it does reach across: Core subscribes to
      the whole branch with `dispatcher.map("/channel/*")`, so that template
      has to match `/channel/0/eq/high`.

    Everything else is escaped, which matters more than it looks: `fx-send`,
    `SCROLL_X+` and `/scroll/x/-` all carry characters a regular expression
    would otherwise read as syntax.
    """
    pattern = "".join(
        ".*" if piece == "*"
        else "[^/]+" if _PLACEHOLDER.fullmatch(piece)
        else re.escape(piece)
        for piece in re.split(r"(\{[^}]*\}|@|\*)", template) if piece)
    compiled = re.compile(f"^{pattern}$")
    return lambda address: compiled.match(address) is not None


def load(path=None) -> Dict[str, Any]:
    """The register on disk, or an empty one that says what went wrong.

    Never raises. Every shape a file can arrive in is a shape it may arrive
    in: missing, half-written, written by a version with a field more or a
    field fewer, or not a register at all.
    """
    path = Path(path) if path is not None else DEFAULT_REGISTER

    try:
        raw = json.loads(path.read_text())
    except (OSError, ValueError) as problem:
        return _empty(str(problem))

    if not isinstance(raw, dict) or not isinstance(raw.get("entries"), list):
        return _empty(f"{path} is not a register")

    entries = []
    for item in raw["entries"]:
        if not isinstance(item, dict) or not item.get("address"):
            continue
        entries.append({field: item.get(field, "") for field in FIELDS})

    return {"problem": "",
            "devices": [name for name in raw.get("devices", [])
                        if isinstance(name, str)],
            "directions": [name for name in raw.get("directions", [])
                           if isinstance(name, str)],
            "entries": entries}


def _empty(problem: str) -> Dict[str, Any]:
    return {"problem": problem, "devices": [], "directions": [],
            "entries": []}


def with_counts(register: Dict[str, Any],
                counts: Dict[str, int]) -> Dict[str, Any]:
    """The register with what each address has actually carried.

    `seen` is the field the page draws a dead wire from, and it is false
    rather than absent when an address has never arrived -- the distinction
    `a3_core_traffic.seen()` cannot make, because a row that was never
    created has no zero to show.

    `seen` and `unseen` are counted here rather than in the page for the same
    reason the matching is: it is arithmetic over the whole list, and the page
    filters that list by device a moment later.
    """
    matched = [matcher(item["address"]) for item in register["entries"]]
    shapes = list(counts.items())

    entries: List[Dict[str, Any]] = []
    for item, match in zip(register["entries"], matched):
        total = sum(count for address, count in shapes if match(address))
        out = dict(item)
        out["count"] = total
        out["seen"] = total > 0
        entries.append(out)

    alive = sum(1 for item in entries if item["seen"])
    return {"problem": register.get("problem", ""),
            "devices": list(register.get("devices", [])),
            "directions": list(register.get("directions", [])),
            "entries": entries,
            "seen": alive,
            "unseen": len(entries) - alive}
