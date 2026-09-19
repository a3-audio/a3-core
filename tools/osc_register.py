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


#: Names that hold the *result* of a table lookup, not a part of an address.
#:
#: Same case as the subscript in _template below, and the same reason to drop
#: it: whatever the table can yield is in the register already, so a row for
#: the expression itself is an address nobody speaks. It needs saying
#: separately because a lookup does not have to be written as a subscript --
#: a3-mixer.py put one in a local on 2026-09-19 and the register promptly grew
#: "/channel/{ch}/{function}".
#:
#: Coupling to a variable's name is exactly what went wrong that day, so this
#: is pinned by a test rather than left to be noticed.
_LOOKUP_NAMES = ("function",)


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
        if node.id in _LOOKUP_NAMES:
            return None
        return _placeholder(node.id)

    # A subscript (`CHANNEL_BUTTONS[index]`) is a lookup into a table this
    # tool reads separately, so the names it can yield are already in the
    # register. Rendering it as a placeholder here would add a row for an
    # address that does not exist.
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
#:
#: `CHANNEL_BUTTONS` lives in a3_mixer_panel.py, not in a3-mixer.py: the keys
#: moved out on 2026-09-19 so that a test could reach them at all. That rename
#: is also the warning attached to this list -- a reader that follows a dict by
#: *name* is coupled to the name, and nothing failed when it changed. The
#: register simply stopped finding pfl and fx and recorded what the handler
#: literally writes, `/channel/{ch}/{function}`, an address nobody speaks.
#: `test_the_key_that_taps_is_not_a_channel_address` is what notices now.
_MIXER_CHANNEL_TABLES = ("analog_pots_per_channel_to_osc_param",
                         "CHANNEL_BUTTONS")

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

#: A C++ string literal. Applied only to text _without_cpp_comments() has been
#: through: both C++ sources here explain themselves at length and their prose
#: quotes addresses, `/vu/0` among them.
_CPP_STRING = re.compile(r'"([^"]*)"')


def _without_cpp_comments(text):
    """The same text with every comment blanked out, line for line.

    Blanked rather than removed so the line numbers stay the ones a reader
    will find in the file -- the `source` column is the whole point of the
    register being checkable.

    It tracks string literals rather than just splitting on `//`, because a
    `//` inside a literal is not a comment and a URL in a string would
    otherwise swallow the rest of its line.
    """
    out = []
    state = "code"
    index = 0
    while index < len(text):
        char = text[index]
        pair = text[index:index + 2]

        if state == "code":
            if pair == "//":
                state = "line comment"
                out.append("  ")
                index += 2
                continue
            if pair == "/*":
                state = "block comment"
                out.append("  ")
                index += 2
                continue
            if char == '"':
                state = "string"
            out.append(char)
        elif state == "string":
            out.append(char)
            if char == "\\":
                # The escaped character cannot end the literal.
                out.append(text[index + 1:index + 2])
                index += 2
                continue
            if char == '"':
                state = "code"
        elif state == "line comment":
            if char == "\n":
                state = "code"
                out.append(char)
            else:
                out.append(" ")
        else:                       # block comment
            if pair == "*/":
                state = "code"
                out.append("  ")
                index += 2
                continue
            out.append(char if char == "\n" else " ")

        index += 1

    return "".join(out)


def from_motion(text, label="OscAddresses.hh"):
    """What A3 Motion speaks, out of the header that declares it.

    Read as text rather than compiled: this is the one source that is C++, and
    a register that needed a JUCE toolchain to regenerate would be a register
    nobody regenerates. The shape it relies on is narrow and stated in
    _MOTION_FIELD -- a declaration, then string literals until the `};`.
    """
    found = []
    field = None

    for number, line in enumerate(
            _without_cpp_comments(text).splitlines(), start=1):
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

        for address in _CPP_STRING.findall(line):
            if _is_address(address):
                found.append(entry(address, "motion",
                                   MOTION_DIRECTIONS[field],
                                   f"{label}:{number}"))

        if "};" in line:
            field = None

    return found


# ------------------------------------------------------------- layout.json

#: Who each of Core's own templates is for. Core builds all of them, so the
#: device is the counterpart at the other end rather than the author -- which
#: is the same thing the window's "Gegenstelle" column says, and the axis the
#: page filters on.
#:
#: A key missing from here raises: a new template in layout.json is a new
#: conversation with somebody, and which somebody is not a thing to default.
LAYOUT_DEVICES = {
    # The recall replays the position and the crossfade to Motion, using this
    # template. See a3_core_recall.remembered_messages.
    "channel_control": "motion",
    "dualdelay_bpm": "dualdelay",
    "fx_mode_led": "mixer",
    "led_fx": "mixer",
    "led_pfl": "mixer",
    "fx_param": "reaper",
    "track_mute": "reaper",
    "track_send": "reaper",
    "track_volume": "reaper",
}


def from_layout(text, label="layout.json"):
    """Core's own address templates, out of the layout it ships with.

    The line is found by searching for the key rather than taken from a
    parser: `json` reports no positions, and a register whose `source` column
    said only "layout.json" would not be checkable by the reader it is for.
    """
    addresses = json.loads(text).get("addresses", {})
    lines = text.splitlines()
    found = []

    for name, template in sorted(addresses.items()):
        if name not in LAYOUT_DEVICES:
            raise ValueError(
                f"{label}: no device is recorded for {name} -- add it to "
                f"LAYOUT_DEVICES, which is where this has to be decided "
                f"rather than guessed")
        quoted = f'"{name}"'
        number = next((index for index, line in enumerate(lines, start=1)
                       if quoted in line), 0)
        found.append(entry(template, LAYOUT_DEVICES[name], OUT,
                           f"{label}:{number}"))

    return found


# -------------------------------------------------------------- a3-core.py

#: Which device each of Core's OSC clients talks to. The name of the variable
#: is the only thing that says so, which is why it is written down here.
#:
#: `client` is the loop variable over udp_clients_iem -- the IEM plug-ins have
#: their own OSC receivers and Core writes straight to them, which is why the
#: position never comes back (see a3_core_recall).
CORE_CLIENTS = {
    "osc_reaper": "reaper",
    "osc_a3mixer": "mixer",
    "osc_a3motion": "motion",
    "osc_dualdelay": "dualdelay",
    "client": "iem",
}

#: The handlers that are actually mapped, and what the word they compare
#: against is an address for. `%s` is the word.
#:
#: **param_handler is deliberately absent.** It is mapped nowhere and calls
#: three functions that do not exist, so nothing it compares against can ever
#: arrive; listing it would put addresses in the register that no running code
#: can receive, which is the opposite of what the register is for.
CORE_HANDLED = {
    ("osc_handler_channel", "parameter"): "/channel/{ch}/%s",
    ("osc_handler_channel", "eq_parameter"): "/channel/{ch}/eq/%s",
    ("osc_handler_master", "parameter"): "/master/%s",
    ("osc_handler_fx", "parameter"): "/fx/%s",
}

#: A branch that is not an address of its own. `parameter == "eq"` only opens a
#: second comparison on the word after it, so the three real addresses are
#: /channel/n/eq/high, /mid and /low -- and nothing ever arrives on
#: /channel/n/eq. Left in, it would read as an address nobody sends, which is
#: what this register calls a dead wire.
CORE_BRANCHES = ("/channel/{ch}/eq",)


def _bindings(tree):
    """Every name that is bound to a string, with the line it was bound on.

    Needed twice over. The addresses Core listens on are mapped through their
    constants -- `dispatcher.map(OSC_ADDRESS_RECALL, ...)` -- and a register
    printing the constant's name instead of its value would not answer the one
    question it is asked. And the two IEM addresses are built into a local
    first (`addr = f"/MultiEncoder/azimuth{channel_index}"`) and sent on the
    next line.

    The line is kept because that local is assigned twice in one function,
    once per address, and the two sends differ only in which assignment
    precedes them -- see _resolve.
    """
    found = []
    for node in ast.walk(tree):
        if isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
            targets = [node.target]
        elif isinstance(node, ast.Assign):
            targets = [t for t in node.targets if isinstance(t, ast.Name)]
        else:
            continue
        template = _template(node.value)
        if template is None:
            continue
        for target in targets:
            found.append((node.lineno, target.id, template))
    return sorted(found)


def _resolve(bindings, name, lineno):
    """What `name` held at `lineno`: the nearest binding at or before it.

    Nearest-preceding rather than last-wins, because `addr` is assigned the
    azimuth address and then the elevation one in the same function, and
    last-wins would report both sends as the elevation.
    """
    holds = [template for at, bound, template in bindings
             if bound == name and at <= lineno]
    return holds[-1] if holds else None


def _receiver(func):
    """The name of the object a method is called on, or None."""
    if isinstance(func, ast.Attribute) and isinstance(func.value, ast.Name):
        return func.value.id
    return None


def from_core(text, label="a3-core.py"):
    """What Core listens for, and what it sends to whom.

    Read out of the syntax tree rather than by importing: importing a3-core.py
    opens sockets and starts a server, which is why every test in this repo
    that needs something out of it walks the tree instead.
    """
    tree = ast.parse(text)
    bindings = _bindings(tree)
    found = []

    for function in ast.walk(tree):
        if not isinstance(function, ast.FunctionDef):
            continue
        for node in ast.walk(function):
            if not isinstance(node, ast.Compare) or len(node.comparators) != 1:
                continue
            if not isinstance(node.ops[0], ast.Eq):
                continue
            if not isinstance(node.left, ast.Name):
                continue
            template = CORE_HANDLED.get((function.name, node.left.id))
            word = node.comparators[0]
            if template is None or not isinstance(word, ast.Constant):
                continue
            if not isinstance(word.value, str):
                continue
            address = template % word.value
            if address in CORE_BRANCHES:
                continue
            found.append(entry(address, "core", IN, _at(label, word)))

    for node in ast.walk(tree):
        if not isinstance(node, ast.Call) or not node.args:
            continue

        address = _template(node.args[0])
        if isinstance(node.args[0], ast.Name):
            address = _resolve(bindings, node.args[0].id, node.lineno)
        if address is None or not _is_address(address):
            continue

        receiver = _receiver(node.func)
        attribute = getattr(node.func, "attr", None)

        if attribute == "map":
            found.append(entry(address, "core", IN, _at(label, node)))
        elif attribute == "send_message":
            if receiver not in CORE_CLIENTS:
                raise ValueError(
                    f"{_at(label, node)}: {receiver} sends {address} and "
                    f"nothing says which device that is -- add it to "
                    f"CORE_CLIENTS")
            found.append(entry(address, CORE_CLIENTS[receiver], OUT,
                               _at(label, node)))

    return found


# ------------------------------------------------------- a3-core.ReaperOSC

#: A line of REAPER's pattern file: an action name, then its patterns. A
#: pattern is a type letter and a path -- `n/track/volume` is the normalised
#: value of the track volume, and the letter is not part of the address.
_REAPER_LINE = re.compile(r"^([A-Z][A-Z0-9_+-]*)\s+(.*)$")


def from_reaper(text, label="a3-core.ReaperOSC"):
    """Everything REAPER understands, out of the pattern file Core ships.

    Direction is BOTH throughout, and that is the file's own doing: one line
    lists the patterns for a control, and REAPER accepts them as commands and
    reports on them. The note carries the action name -- FX_WETDRY -- because
    that is the word a reader is looking for, while the pattern is what goes
    on the wire.
    """
    found = []

    for number, line in enumerate(text.splitlines(), start=1):
        if line.startswith("#"):
            continue
        match = _REAPER_LINE.match(line)
        if match is None:
            continue
        action, patterns = match.groups()
        for pattern in patterns.split():
            if "/" not in pattern:
                continue            # DEVICE_TRACK_COUNT 27 and its kind
            address = pattern[pattern.index("/"):]
            if _is_address(address):
                found.append(entry(address, "reaper", BOTH,
                                   f"{label}:{number}", note=action))

    return found


# --------------------------------------------------------- the beat-analyzer

#: Which way each of the analyzer's addresses runs, from Core's side.
#:
#: `/beat` is the only one Core is a party to. The VU meters go straight to
#: the mixer and to Motion, and `/tap` and `/clockmode` are what the analyzer
#: itself listens for -- Core sends neither.
ANALYZER_DIRECTIONS = {
    "/beat": IN,
    "/vu/": ASIDE,
    "/tap": ASIDE,
    "/clockmode": ASIDE,
}


def from_beat_analyzer(text, label="beat-analyzer"):
    """The clock and the meters, out of the analyzer's C++.

    The addresses are written out as literals in a handful of places, so this
    is a scan for literals that look like an address. "Look like" is doing
    work: a printf format string starts with a slash too, and what rules it
    out is the space in it (see ADDRESS).
    """
    found = []

    for number, line in enumerate(
            _without_cpp_comments(text).splitlines(), start=1):
        for address in _CPP_STRING.findall(line):
            if not _is_address(address):
                continue
            if address not in ANALYZER_DIRECTIONS:
                raise ValueError(
                    f"{label}:{number}: no direction is recorded for "
                    f"{address} -- add it to ANALYZER_DIRECTIONS, which is "
                    f"where this has to be decided rather than guessed")
            found.append(entry(address, "beat-analyzer",
                               ANALYZER_DIRECTIONS[address],
                               f"{label}:{number}"))

    return found


# ------------------------------------------------------------- the whole thing

#: Which reader each source is read by.
READERS = {
    "layout": from_layout,
    "core": from_core,
    "reaper": from_reaper,
    "mixer": from_mixer,
    "motion": from_motion,
    "beat-analyzer": from_beat_analyzer,
}

#: Where Core's own files sit inside this repo.
PACKAGE = "platform-config/debian-x86_64/a3-core/home/aaa/.local"

#: Where the finished register is written, and the only one of these files the
#: installed Core ever reads.
REGISTER = f"{PACKAGE}/share/a3-core/osc-register.json"

#: What the file says about itself, so somebody who opens it knows not to edit
#: it. The register is generated; a hand edit would be erased by the next run
#: and would make the drift test fail without saying why.
COMMENT = ("Generated by tools/osc_register.py from the sources listed in "
           "each entry. Do not edit by hand: tools/tests/test_osc_register.py "
           "regenerates this and compares.")


def source_paths(root, workspace=None):
    """Every file the register is read from, by source key.

    `root` is the a3-core checkout; `workspace` is the folder the sibling repos
    sit in, which defaults to root's parent. Values are lists because the
    beat-analyzer writes its four addresses across several files.
    """
    workspace = Path(workspace) if workspace else Path(root).parent
    root = Path(root)
    analyzer = workspace / "beat-analyzer"

    return {
        "layout": [root / PACKAGE / "share/a3-core/layout.json"],
        "core": [root / PACKAGE / "bin/a3-core.py"],
        "reaper": [root / PACKAGE
                   / "share/a3-core/config/REAPER/OSC/a3-core.ReaperOSC"],
        "mixer": [workspace / "a3-mixer/software/scripts/a3-mixer.py",
                  workspace / "a3-mixer/software/scripts/a3_mixer_panel.py"],
        "motion": [workspace
                   / "a3-motion-ui/src/a3-motion-engine/OscAddresses.hh"],
        "beat-analyzer": sorted(analyzer.glob("src/**/*.cpp"))
                         + sorted(analyzer.glob("include/**/*.h")),
    }


def missing(paths):
    """The source keys this machine cannot read, if any.

    A key whose list is empty counts as missing too: that is what an absent
    beat-analyzer checkout looks like through a glob, and a register built
    without it would be short by four addresses while looking complete.
    """
    absent = []
    for key, files in sorted(paths.items()):
        if not files or not all(Path(path).exists() for path in files):
            absent.append(key)
    return absent


def merge(entries):
    """One row per address, device and direction, in a stable order.

    Two sources saying the same thing collapse -- the beat-analyzer writes
    `/beat` in five places -- and the first source in sort order is the one
    kept, so the file does not churn between runs.

    Two sources saying *different* things do not collapse, and that is the
    point: `/channel/{ch}/gain` stays three rows, one for the mixer that sends
    it, one for Motion that also sends it, and one for Core that listens. The
    device filter is only worth having if those are separate.
    """
    best = {}
    for item in sorted(entries, key=lambda row: tuple(row[f] for f in FIELDS)):
        key = (item["address"], item["device"], item["direction"])
        best.setdefault(key, item)
    return [best[key] for key in sorted(best)]


def build(paths):
    """The whole register, read from the files `paths` names.

    Nothing here is dated or counted: the file is compared byte for byte
    against a fresh build by the test, so a timestamp in it would make every
    run look like drift.
    """
    entries = []
    for key, reader in READERS.items():
        for path in paths.get(key, ()):
            entries.extend(reader(Path(path).read_text(), Path(path).name))

    return {"_comment": COMMENT,
            "devices": list(DEVICES),
            "directions": list(DIRECTIONS),
            "entries": merge(entries)}


def as_text(register):
    """The register as it is written: two-space JSON with a trailing newline.

    One entry per line, because a register is read in a diff as often as in a
    browser and `indent=2` would put every field on its own line -- a
    five-field change per moved address.
    """
    lines = [json.dumps(item, sort_keys=True) for item in register["entries"]]
    head = {key: value for key, value in register.items() if key != "entries"}
    body = ",\n    ".join(lines)
    return (json.dumps(head, indent=2)[:-2] + ",\n"
            + '  "entries": [\n    ' + body + "\n  ]\n}\n")


def main(argv):
    root = Path(__file__).resolve().parents[1]
    paths = source_paths(root)

    absent = missing(paths)
    if absent:
        print(f"cannot build the register here: {', '.join(absent)} is not "
              f"beside this checkout. A short register that looked complete "
              f"would be worse than none, so nothing was written.")
        return 1

    register = build(paths)
    target = root / REGISTER
    target.write_text(as_text(register))
    print(f"{len(register['entries'])} addresses -> {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
