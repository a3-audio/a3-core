#!/usr/bin/env python3
"""The addresses this system can speak, lifted out of the code that speaks them.

**Why this is not the traffic window.** The window counts what flew past Core.
It cannot answer "what is the address for a channel bus's FX send", because an
address nobody has sent has no row there at all -- and on 2026-09-12 the
answer to that question was three rows, both of them true. A log shows what
happened; a register shows what is possible. They are two different things and
mixing them is what sent the maintainer looking in the wrong place.

So the register is **generated** from the six places an address is actually
written, and checked in as share/a3-core/osc-register.json. Core reads only
the finished file: the installed copy under ~/.local has no neighbouring repos
beside it. The price is that the file can go stale, which is what
tools/tests/test_osc_register.py exists to catch -- it regenerates and
compares, and skips out loud where a repo is missing.

**Every reader here takes text, not a path.** That is the whole reason this is
testable: a reader is a pure function from one file's contents to a list of
entries, so the tests state what each source's shape means without needing the
workspace. Only build() touches the filesystem.

**What an entry claims, and what it does not.** `address` is the template as
the source writes it, with placeholders; it is not a pattern to match against.
`source` is file:line so a reader can go and look, which matters because most
of these are reconstructed rather than quoted -- a3-mixer.py builds
`/channel/0/gain` by concatenating three pieces, and no line of it contains
that string. `direction` is from Core's side, and has four values rather than
two because the sources honestly say four different things; see DIRECTIONS.
"""

import ast
import json
import re
import sys
from pathlib import Path

#: Core receives it.
IN = "in"

#: Core sends it.
OUT = "out"

#: Both, and the source does not distinguish. REAPER's pattern file is the
#: case: one line lists the patterns for a control, and REAPER both accepts
#: them as commands and reports on them. Splitting each into an `in` and an
#: `out` row would double the register to say nothing the file says.
BOTH = "both"

#: Core is not a party to it. The beat-analyzer's `/vu/*` goes straight to the
#: mixer; A3 Motion's `/StereoEncoder/*` goes straight to an IEM plug-in.
#: Calling those `in` or `out` would put Core in a conversation it never
#: hears, and "never seen by Core" is then wrongly read as a dead wire when it
#: is the normal case.
ASIDE = "aside"

DIRECTIONS = (IN, OUT, BOTH, ASIDE)

#: What every entry carries. Checked by the tests, and by build() before it
#: writes, so a reader cannot quietly invent or drop a field.
FIELDS = ("address", "device", "direction", "source", "note")

#: The devices an entry can belong to. This is the axis the page filters on,
#: so a new name has to be added here deliberately rather than appearing
#: because a reader guessed one.
#:
#: `core` is for the addresses Core itself serves -- what it listens for,
#: which any controller may send. The others are the counterpart Core is
#: talking to, which is the same vocabulary the window's "Gegenstelle" column
#: already uses.
DEVICES = ("beat-analyzer", "core", "dualdelay", "iem", "mixer", "motion",
           "reaper")

#: How a source's variable name is spelled in an address template.
#:
#: Only the channel number is renamed, and only because three sources spell
#: it three ways (`track` in a3-mixer.py, `channel_index` in a3-core.py,
#: `{ch}` in OscAddresses.hh) while meaning one thing -- a register where the
#: same placeholder reads differently per source is a register a reader has to
#: translate. Every other name is kept as the source wrote it: `{track_input}`
#: and `{FX_INDEX_GAIN}` say which layout field and which constant decide the
#: number, and that is more than `{n}` would say.
PLACEHOLDERS = {
    "track": "ch",
    "channel": "ch",
    "channel_index": "ch",
}

#: What an address may look like. Used to refuse the things that look like one
#: and are not: a printf format string ("/vu/3 | peak %.3f"), a device path
#: ("/dev/ttyACM0"), a URL's tail. No spaces, and a leading slash.
ADDRESS = re.compile(r"^/[A-Za-z0-9_{}*@/.+-]*$")


def entry(address, device, direction, source, note=""):
    """One row of the register, with every field present."""
    if device not in DEVICES:
        raise ValueError(f"unknown device {device!r} for {address}")
    if direction not in DIRECTIONS:
        raise ValueError(f"unknown direction {direction!r} for {address}")
    return {"address": address, "device": device, "direction": direction,
            "source": source, "note": note}


def _placeholder(name):
    return "{" + PLACEHOLDERS.get(name, name) + "}"


def _at(label, node):
    return f"{label}:{node.lineno}"


def _is_address(text):
    return bool(text) and text.startswith("/") and bool(ADDRESS.match(text))


def _template(node):
    """The address a string expression stands for, or None.

    Handles the three shapes the sources use: a plain literal, an f-string,
    and a `+` concatenation. The last two carry names, and a name becomes a
    placeholder rather than being dropped -- `"/channel/" + track + "/enc"` is
    an address this register would otherwise miss entirely, because no line of
    a3-mixer.py contains it.
    """
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return node.value

    if isinstance(node, ast.JoinedStr):
        parts = []
        for piece in node.values:
            if isinstance(piece, ast.Constant) and isinstance(piece.value, str):
                parts.append(piece.value)
            elif isinstance(piece, ast.FormattedValue):
                parts.append(_placeholder(_names_in(piece.value)))
            else:
                return None
        return "".join(parts)

    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        left, right = _template(node.left), _template(node.right)
        if left is None or right is None:
            return None
        return left + right

    if isinstance(node, ast.Name):
        return _placeholder(node.id)

    # A subscript (`button_per_channel_to_osc_param[index]`) is a lookup into
    # a table this tool reads separately, so the names it can yield are
    # already in the register. Rendering it as a placeholder here would add a
    # row for an address that does not exist.
    return None


def _names_in(node):
    """The name an f-string's `{...}` is built from, for the placeholder.

    An expression rather than a bare name -- `_layout.fx_param('enc_pot_1')`
    -- is rendered as the source wrote it, with the quotes stripped, because
    that is what tells a reader which parameter it is.
    """
    if isinstance(node, ast.Name):
        return node.id
    return ast.unparse(node).replace("'", "").replace('"', "")


# ---------------------------------------------------------------- a3-mixer.py

#: The mixer's per-channel tables, and what the strip calls the control. Both
#: hold a bare word that is pasted onto `/channel/<n>/`; the dict name says
#: whether it came from a pot or a button, which is the only difference.
_MIXER_CHANNEL_TABLES = ("analog_pots_per_channel_to_osc_param",
                         "button_per_channel_to_osc_param")

#: The mixer's master table, which holds whole addresses already.
_MIXER_MASTER_TABLE = "master_pots_to_osc_message"

#: What the mixer subscribes to that Core does not send. The beat-analyzer
#: sends `/vu/*` to the mixer directly -- Core never relays it -- so the one
#: honest direction for it is ASIDE. The LED addresses in the same dispatcher
#: do come from Core (see layout.json) and are OUT.
_MIXER_NOT_FROM_CORE = ("/vu/",)


def from_mixer(text, label="a3-mixer.py"):
    """What the A3 Mixer's control script speaks.

    Three dicts and a serial handler. The dicts are the authority on the
    channel strip: the handler pastes `"/channel/" + track + "/"` onto a
    value out of one of them, so the addresses exist only as three pieces and
    have to be put back together here.
    """
    tree = ast.parse(text)
    found = []

    for node in ast.walk(tree):
        if isinstance(node, ast.Assign):
            names = [target.id for target in node.targets
                     if isinstance(target, ast.Name)]
            if not isinstance(node.value, ast.Dict):
                continue
            for key, value in zip(node.value.keys, node.value.values):
                if not isinstance(value, ast.Constant):
                    continue
                if any(name in _MIXER_CHANNEL_TABLES for name in names):
                    found.append(entry(f"/channel/{{ch}}/{value.value}",
                                       "mixer", IN, _at(label, value)))
                elif _MIXER_MASTER_TABLE in names and _is_address(value.value):
                    found.append(entry(value.value, "mixer", IN,
                                       _at(label, value)))

        if not isinstance(node, ast.Call) or not node.args:
            continue
        if not isinstance(node.func, ast.Attribute):
            continue

        address = _template(node.args[0])
        if address is None or not _is_address(address):
            continue

        if node.func.attr == "send_message":
            found.append(entry(address, "mixer", IN, _at(label, node)))
        elif node.func.attr == "map":
            aside = address.startswith(_MIXER_NOT_FROM_CORE)
            found.append(entry(address, "mixer", ASIDE if aside else OUT,
                               _at(label, node)))

    return found


# ----------------------------------------------------------- OscAddresses.hh

#: Which way round each of A3 Motion's address fields runs, from Core's side.
#:
#: Written out rather than read off the header's `// Outgoing` comments: the
#: comments group three destinations under one word, and the difference
#: between them is the whole point of ASIDE. A field missing from here raises
#: rather than defaulting, so a new address in the header arrives as a failing
#: test instead of as a row filed under a direction nobody chose.
#:
#: `beatOut`, `tap` and `clockMode` are IN because Motion's sender points at
#: Core, which is where its mixer messages go. Core routes only `/beat` of the
#: three today -- the other two arrive and fall on the floor, because Core's
#: main dispatcher has no default handler -- and that is exactly the kind of
#: finding this register exists to make visible, not a reason to call them
#: something else here.
MOTION_DIRECTIONS = {
    "channelAzimuth": IN,
    "channelElevation": IN,
    "channelPot1": IN,
    "channelPot2": IN,
    "channelThreeD": IN,
    "mixerChannel": IN,
    "mixerMaster": IN,
    "mixerFilter": IN,
    "stateRecall": IN,
    "beatOut": IN,
    "tap": IN,
    "clockMode": IN,
    # SpatBackendIEM writes to the plug-in's own OSC port. Core never sees it.
    "iemAzimuth": ASIDE,
    "iemElevation": ASIDE,
    # The beat-analyzer's, straight to Motion. Core is not in this path
    # either: it has its own /beat tap on its own port.
    "vuPrefix": ASIDE,
    "energyRms": ASIDE,
    "beatIn": ASIDE,
}

#: A field declaration in OscAddresses.hh: one address or a table of them.
_MOTION_FIELD = re.compile(
    r"^\s*(?:juce::String|std::array\s*<\s*juce::String\s*,[^>]*>)\s+(\w+)\s*\{")

#: A C++ string literal. Applied only inside a declaration, and only after
#: `//` has been cut off the line -- the header explains itself at length and
#: its prose quotes addresses.
_CPP_STRING = re.compile(r'"([^"]*)"')


def from_motion(text, label="OscAddresses.hh"):
    """What A3 Motion speaks, out of the header that declares it.

    Read as text rather than compiled: this is the one source that is C++, and
    a register that needed a JUCE toolchain to regenerate would be a register
    nobody regenerates. The shape it relies on is narrow and stated in
    _MOTION_FIELD -- a declaration, then string literals until the `};`.
    """
    found = []
    field = None

    for number, line in enumerate(text.splitlines(), start=1):
        start = _MOTION_FIELD.match(line)
        if start is not None:
            field = start.group(1)
            if field not in MOTION_DIRECTIONS:
                raise ValueError(
                    f"{label}:{number}: no direction is recorded for "
                    f"{field} -- add it to MOTION_DIRECTIONS, which is where "
                    f"this has to be decided rather than guessed")

        if field is None:
            continue

        code = line.split("//")[0]
        for address in _CPP_STRING.findall(code):
            if _is_address(address):
                found.append(entry(address, "motion",
                                   MOTION_DIRECTIONS[field],
                                   f"{label}:{number}"))

        if "};" in line:
            field = None

    return found


def build(paths):
    """The whole register, read from the files a workspace has.

    `paths` names every source by key; a key that is absent is skipped, which
    is what lets this run where a neighbouring repo is not checked out. The
    test says so out loud rather than comparing against a short register.
    """
    raise NotImplementedError


if __name__ == "__main__":
    sys.exit("not finished yet")
