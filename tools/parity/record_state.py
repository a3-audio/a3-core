# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records what B0's a3_core_state, a3_core_evening and a3_core_recall do with the files the rig
really has (tools/parity/data/state/*.json), for the C++ Core to match.

    A3_CORE_BASELINE=<exported a3-core B0> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_state.py tests/core/data/parity/state

Writes state.json, evening.json and recall.json into the directory given. Each input file is
recorded by name with its text, so the C++ tests need no copy of the inputs.
"""
import json
import os
import sys
import tempfile
from dataclasses import dataclass
from enum import Enum
from pathlib import Path

BASELINE = Path(os.environ["A3_CORE_BASELINE"])
PACKAGE = BASELINE / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))
os.environ.pop("A3_NETWORK", None)
os.environ.pop("A3_OSC_TRUTH", None)

import a3_osc  # noqa: E402
from a3_core_evening import evening_state, replayable  # noqa: E402
from a3_core_recall import Relayed, recall_messages  # noqa: E402
from a3_core_state import StateFile, apply_state, state_of  # noqa: E402

INPUTS = Path(__file__).resolve().parent / "data/state"
TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")


class FXMode(Enum):
    LOW_PASS = 0
    HIGH_PASS = 1


@dataclass
class Channel:
    toggle_fx: bool = False
    toggle_cue: bool = False
    azimuth: float = None
    elevation: float = None
    three_d: float = None


@dataclass
class Master:
    fx_mode: FXMode = FXMode.LOW_PASS
    stem_cue: bool = False
    phones_mix: float = 0.0
    return_cue: bool = False


class FakeStems:
    def __init__(self, data):
        self.data = data

    def as_data(self):
        return self.data


def typed(value):
    """A Python value as [type, value] so a float 1.0 and an int 1 stay apart in the JSON."""
    return [type(value).__name__, value]


def read_text(path):
    try:
        return path.read_text()
    except (OSError, ValueError):
        return ""


def rig(channels=4):
    return [Channel() for _ in range(channels)], Master()


def loaded(channels, master):
    return {"channels": [{"toggle_fx": bool(c.toggle_fx), "toggle_cue": bool(c.toggle_cue),
                          "three_d": c.three_d} for c in channels],
            "master": {"fx_mode": master.fx_mode.value, "phones_mix": float(master.phones_mix),
                       "return_cue": bool(master.return_cue)}}


def load_via_state_file(text):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "state.json"
        path.write_text(text)
        return StateFile(path).load()


def written_by_state_file(state):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "state.json"
        file = StateFile(path, delay=3600)
        file.remember(state)
        file.flush()
        return path.read_text()


def record_states():
    cases = {}
    for path in sorted(INPUTS.glob("state-*.json")):
        text = read_text(path)
        state = load_via_state_file(text)
        channels, master = rig()
        entry = {"text": text}
        try:
            apply_state(state, channels, master)
        except Exception as problem:   # a file Python itself cannot take
            entry["raises"] = type(problem).__name__
            cases[path.stem] = entry
            continue
        entry["loaded"] = loaded(channels, master)
        if isinstance(state, dict):
            stems = FakeStems(state.get("stems")) if "stems" in state else None
            try:
                entry["state_of"] = json.dumps(state_of(channels, master, stems), indent=2) + "\n"
            except Exception as problem:
                entry["state_of_raises"] = type(problem).__name__
            entry["file_written"] = written_by_state_file(state)
        cases[path.stem] = entry
    return cases


def relayed_of(notes):
    relayed = Relayed()
    for address, value in notes:
        relayed.note(address, value)
    return relayed


def record_evenings():
    cases = {}
    for path in sorted(INPUTS.glob("evening-*.json")):
        text = read_text(path)
        state = load_via_state_file(text)
        entry = {"text": text}
        try:
            entry["replay"] = [[address, *typed(value)] for address, value in replayable(TRUTH, state)]
        except Exception as problem:
            entry["raises"] = type(problem).__name__
        cases[path.stem] = entry

    notes = [("/channel/1/gain", 0.5), ("/master/volume", 0.8), ("/channel/2/volume", 1),
             ("/filter/frequency", 0.1), ("/channel/3/aux-send", True), ("/master/aux-return", "xü"),
             ("/channel/1/gain", 1e-05)]
    relayed = relayed_of(notes)
    written = {"notes": [[address, *typed(value)] for address, value in notes],
               "text": json.dumps(evening_state(relayed), indent=2) + "\n"}
    return {"files": cases, "written": written}


def positions_applied(channels, positions):
    for channel, (azimuth, elevation) in zip(channels, positions):
        channel.azimuth = azimuth
        channel.elevation = elevation


def record_recalls():
    cases = []
    relays = {
        "cold": [],
        "relayed": [("/channel/1/gain", 0.5), ("/master/volume", 0.8), ("/channel/2/volume", 1),
                    ("/master/aux-return", "x"), ("/channel/1/gain", 0.7)],
    }
    positions = {
        "none": [],
        "some": [(-37.5, 12.0), (None, None), (0.0, 0.0), (None, -90.0)],
    }
    for state_name in ("state-current", "state-pfl", "state-odd", "state-numbers"):
        text = read_text(INPUTS / f"{state_name}.json")
        for relay_name, notes in relays.items():
            for position_name, where in positions.items():
                channels, master = rig()
                apply_state(load_via_state_file(text), channels, master)
                positions_applied(channels, where)
                messages = list(recall_messages(TRUTH, channels, master, relayed_of(notes)))
                cases.append({
                    "state": state_name, "relayed": [[address, *typed(value)] for address, value in notes],
                    "positions": [list(pair) for pair in where], "relay_name": relay_name,
                    "position_name": position_name,
                    "messages": [[address, *typed(value)] for address, value in messages]})
    return cases


directory = Path(sys.argv[1])
directory.mkdir(parents=True, exist_ok=True)
for name, content in (("state", record_states()), ("evening", record_evenings()), ("recall", record_recalls())):
    (directory / f"{name}.json").write_text(json.dumps(content, indent=1, ensure_ascii=True) + "\n", encoding="utf-8")
