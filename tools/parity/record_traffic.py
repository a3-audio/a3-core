# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records what B0's a3_core_traffic, a3_core_seen, a3_core_register and the serialisers of
a3_core_web (as_json, unknown_as_json) answer, for the C++ Core to match.

    A3_CORE_BASELINE=<exported a3-core B0> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_traffic.py tests/core/data/parity/traffic

Writes traffic.json, seen.json, register.json and a copy of the shipped osc-register.json into the directory given. Clocks are replaced by
a scripted one, so the numbers are exact and the C++ replay can reproduce them.
"""
import json
import math
import os
import sys
import tempfile
from pathlib import Path

BASELINE = Path(os.environ["A3_CORE_BASELINE"])
PACKAGE = BASELINE / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc  # noqa: E402
import a3_core_register  # noqa: E402
import a3_core_seen  # noqa: E402
import a3_core_traffic  # noqa: E402
import a3_core_web  # noqa: E402
from a3_core_traffic import IN, OUT, Traffic, osc_bytes, peer_name  # noqa: E402


class Scripted:
    """time.monotonic()/time.time() that say what the script says."""
    mono = 0.0
    wall = 0.0

    @classmethod
    def monotonic(cls):
        return cls.mono

    @classmethod
    def time(cls):
        return cls.wall


a3_core_traffic.time = Scripted
a3_core_seen.time = Scripted


def value_of(spec):
    """["float", 0.5] -> the Python value; ["float", "nan"] and ["bytes", hex] spelled out."""
    kind, raw = spec
    if kind == "float":
        return float(raw)
    if kind == "bytes":
        return bytes.fromhex(raw)
    if kind == "tuple":
        return tuple(raw)
    if kind == "NoneType":
        return None
    return raw


def jsonable(value):
    """What json.dumps keeps of a recorded result: NaN and inf as text, so the file is strict."""
    if isinstance(value, float) and not math.isfinite(value):
        return repr(value)
    if isinstance(value, dict):
        return {key: jsonable(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [jsonable(item) for item in value]
    return value


def play(traffic, ops):
    for op in ops:
        Scripted.mono = op["t"]
        if op["op"] == "seen":
            traffic.seen(IN if op["dir"] == "in" else OUT, op["address"], value_of(op["value"]), op["peer"])
        else:
            traffic.unknown(op["address"], value_of(op["value"]), op["peer"])


def snap(at, address, value, peer="motion", direction="in"):
    return {"op": "seen", "t": at, "dir": direction, "address": address, "value": value, "peer": peer}


def unk(at, address, value, peer="reaper"):
    return {"op": "unknown", "t": at, "address": address, "value": value, "peer": peer}


def record_as_json():
    first = [snap(10.0, "/channel/0/gain", ["float", 0.5]),
             snap(10.25, "/channel/0/gain", ["float", 0.75], "mixer"),
             snap(10.5, "/channel/1/pfl", ["str", "1"], "mixer"),
             snap(10.75, "/channel/1/pfl", ["str", "0"], "mixer", "out"),
             unk(11.0, "/track/3/name", ["str", "kick"]),
             unk(11.25, "/track/3/name", ["float", 0.25])]
    second = [snap(12.0, "/channel/0/gain", ["float", 0.875]),
              snap(12.5, "/channel/0/gain", ["float", 0.0625], "mixer"),
              snap(12.75, "/channel/2/mute", ["bool", True], "motion"),
              snap(13.0, "/channel/2/mode", ["int", 3], "motion", "out"),
              snap(13.25, "/state/none", ["NoneType", None], "motion"),
              snap(13.5, "/odd/nan", ["float", "nan"], "motion"),
              snap(13.75, "/odd/inf", ["float", "inf"], "motion"),
              snap(14.0, "/odd/ninf", ["float", "-inf"], "motion"),
              snap(14.25, "/odd/bytes", ["bytes", "6101270a00ff"], "motion"),
              snap(14.5, "/odd/quote", ["bytes", "6122"], "motion"),
              snap(14.75, "/odd/both", ["bytes", "612722"], "motion"),
              snap(15.0, "/odd/midi", ["tuple", [1, 144, 60, 100]], "motion"),
              snap(15.25, "/odd/text", ["str", "grüße ♪"], "motion"),
              snap(15.5, "/aä/ü", ["int", 7], "10.0.0.9"),
              unk(15.75, "/track/9/name", ["float", "nan"])]
    traffic = Traffic()
    out = {"first_ops": first, "second_ops": second, "looks": {}}
    Scripted.mono = 0
    play(traffic, first)
    Scripted.mono = 12.0 - 0.0625
    one = traffic.snapshot()
    out["looks"]["first"] = {"t": Scripted.mono, "json": jsonable(a3_core_web.as_json(one, None))}
    play(traffic, second)
    Scripted.mono = 16.0
    two = traffic.snapshot(history_since=one["at"])
    out["looks"]["second_delta"] = {
        "t": Scripted.mono, "since": one["at"],
        "json": jsonable(a3_core_web.as_json(two, one))}
    Scripted.mono = 16.5
    three = traffic.snapshot()
    out["looks"]["third_full"] = {"t": 16.5, "json": jsonable(a3_core_web.as_json(three, None))}
    Scripted.mono = 17.0
    unknown = traffic.unknown_snapshot()
    out["unknown_look"] = {"t": 17.0, "json": jsonable(a3_core_web.unknown_as_json(unknown))}
    return out


def record_bytes_and_peers():
    pairs = [("/a", ["float", 0.5]), ("/abcd", ["int", 3]), ("/abc", ["float", 1.0]),
             ("/x", ["str", "1"]), ("/x", ["str", "hello"]), ("/a", ["NoneType", None]),
             ("/a", ["bool", True]), ("/a", ["bool", False]), ("/a/b/c/d/e", ["str", ""]),
             ("/äü", ["str", "üöä"]), ("/b", ["bytes", "010203"]),
             ("/b", ["bytes", ""]), ("/m", ["tuple", [1, 2, 3, 4]]), ("/big", ["int", 2 ** 40])]
    sizes = [[address, spec, osc_bytes(address, value_of(spec))] for address, spec in pairs]
    rigs = [({"mixer": "192.168.43.55", "motion": "192.168.43.54", "reaper": "127.0.0.1"}, None),
            ({"mixer": "127.0.0.1", "motion": "127.0.0.1", "reaper": "127.0.0.1"}, None),
            ({"mixer": "192.168.8.11", "motion": "127.0.0.1", "reaper": "127.0.0.1"}, ["mixer", "motion"]),
            ({"mixer": "192.168.8.11", "motion": "127.0.0.1", "reaper": "127.0.0.1"}, ["reaper"]),
            ({"mixer": "127.0.0.1", "motion": "127.0.0.1", "reaper": "192.168.8.20"}, ["mixer", "motion"]),
            ({"mixer": "a", "motion": "a"}, []),
            ({"mixer": "a"}, ["nobody"])]
    peers = []
    for rig, only in rigs:
        for host in ("192.168.43.55", "127.0.0.1", "10.0.0.9", "a"):
            peers.append({"host": host, "peers": rig, "only": only, "name": peer_name(host, rig, only)})
    return {"sizes": sizes, "peers": peers}


def record_seen():
    out = {}

    # A file Python wrote, restored an hour later.
    traffic = Traffic()
    Scripted.wall = 1_800_000_000.0
    play(traffic, [snap(100.0, "/channel/0/gain", ["float", 0.5], "mixer"),
                   snap(100.5, "/channel/0/gain", ["float", 0.6], "mixer"),
                   snap(101.0, "/track/12/volume", ["float", 0.4], "reaper", "out"),
                   snap(101.5, "/café/♪", ["str", "x"], "mixer"),
                   unk(102.0, "/track/3/name", ["str", "kick"]),
                   unk(102.25, "/track/3/name", ["str", "kick"]),
                   unk(102.5, "/track/4/name", ["str", "snare"])])
    Scripted.mono = 110.0
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "seen.json"
        a3_core_seen.SeenFile(path, traffic).write()
        text = path.read_text()
    out["written_by_python"] = {"wall": Scripted.wall, "mono": 110.0, "text": text}

    Scripted.wall = 1_800_000_000.0 + 3600.0
    Scripted.mono = 50.0
    after = Traffic()
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "seen.json"
        path.write_text(text)
        put = a3_core_seen.SeenFile(path, after).restore()
    Scripted.mono = 60.0
    rows = after.snapshot()
    unknown = after.unknown_snapshot()
    out["restored"] = {
        "wall": Scripted.wall, "mono_at_restore": 50.0, "put": put, "snapshot_at": 60.0,
        "rows": [[r["direction"], r["address"], r["count"], r["peer"], r["last_seen"], r["bytes"]]
                 for r in rows["rows"]],
        "unknown": [[r["address"], r["count"], r["peer"], r["last_seen"]] for r in unknown["rows"]],
        "unknown_messages": rows["unknown_messages"], "created": after.created()}

    # Files of every shape: what comes back, by address.
    shapes = {
        "another-version": {"version": 2, "at": 1.0, "rows": [["in", "/a", 1, "m", 1.0]], "unknown": []},
        "version-as-float": {"version": 1.0, "rows": [["in", "/a", 1, "m", 1.0]], "unknown": []},
        "version-as-true": {"version": True, "rows": [["in", "/a", 1, "m", 1.0]], "unknown": []},
        "no-version": {"rows": [["in", "/a", 1, "m", 1.0]], "unknown": []},
        "rows-not-a-list": {"version": 1, "rows": {"in": 1}, "unknown": "x"},
        "bad-rows": {"version": 1, "rows": [
            ["in", "/a", 1, "m", 1.0], ["in"], "no", ["in", "", 1, "m", 1.0],
            ["in", "/b", "x", "m", 1.0], ["in", "/c", 1, "m", "late"], ["in", "/d", 1, "m", None],
            ["out", "/e", True, "m", 2], ["in", "/f", 1.5, "m", 1.0], ["in", "/g", 1, "m", 1.0, 9],
            ["in", 7, 1, "m", 1.0], ["in", "/h", 3, "m", True]],
            "unknown": [["/u1", 2, "r", 5.0], ["/u2"], ["", 1, "r", 1.0], ["/u3", "x", "r", 1.0],
                        ["/u4", 4, "r", 1.0, 1], ["/a", 9, "r", 1.0], ["/u5", 1, "r", None]]},
        "a-known-address-is-not-unknown": {"version": 1, "rows": [["in", "/k", 4, "m", 5.0],
                                                                  ["out", "/o", 4, "m", 5.0]],
                                           "unknown": [["/k", 9, "r", 5.0], ["/o", 9, "r", 5.0]]},
        "from-the-future": {"version": 1, "rows": [["in", "/a", 1, "m", 1_800_086_400.0]], "unknown": []},
    }
    out["shapes"] = {}
    for name, content in shapes.items():
        Scripted.wall = 1_800_000_000.0
        Scripted.mono = 70.0
        traffic = Traffic()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "seen.json"
            text = json.dumps(content)
            path.write_text(text)
            put = a3_core_seen.SeenFile(path, traffic).restore()
        Scripted.mono = 75.0
        snapshot = traffic.snapshot()
        unknown = traffic.unknown_snapshot()
        out["shapes"][name] = {
            "text": text, "put": put,
            "rows": [[r["direction"], r["address"], r["count"], r["peer"], r["last_seen"]]
                     for r in snapshot["rows"]],
            "unknown": [[r["address"], r["count"], r["peer"], r["last_seen"]] for r in unknown["rows"]],
            "unknown_messages": snapshot["unknown_messages"]}
    return out


TEMPLATES = ["/state/recall", "/channel/{ch}/gain", "/track/@/fx/@/fxparam/@/value", "/MultiEncoder/azimuth{ch}",
             "/channel/*", "/channel/*/led/*", "/channel/{ch}/aux-send", "/scroll/x/+", "/scroll/x/-",
             "/a.b", "/a(b)", "/a{ch", "/a{}/b", "/{x}{y}", "**", "/*/*", "/p/{a}/q/@", "/x/{ch}", "/a b", "",
             "/ä/{ch}", "/a|b", "/a^b$", "/{ch}"]
ADDRESSES = ["/state/recall", "/state/recalling", "/channel/0/gain", "/channel/0/1/gain", "/channel/0/gain\n",
             "/track/0/fx/0/fxparam/0/value", "/track/0/fx/0/value", "/MultiEncoder/azimuth0",
             "/MultiEncoder/elevation0", "/MultiEncoder/azimuth", "/channel/0/eq/high", "/channel/",
             "/channel/2/led/pfl", "/channel/2/pfl", "/channel/1/aux-send", "/scroll/x/+", "/scroll/x/",
             "/scroll/x/-", "/a.b", "/axb", "/a(b)", "/a{ch", "/a{ch}", "/a1/b", "/a/b", "/ab", "/ab/",
             "/x\ny", "/channel/\n/gain", "/channel/a\nb", "/p/1/q/2", "/p/1/2/q/2", "/x/", "/x/1", "/a b",
             "", "/", "/ä/1", "/ä/", "/a|b", "/a^b$", "/1", "/1\n"]
COLLAPSES = ["/track/17/volume", "/track/12/fx/3/fxparam/29/value", "/master/phones_mix",
             "/MultiEncoder/azimuth0", "/1/2/3", "/12", "/12/", "12", "/a12/b", "/12a/b", "/track/1\n",
             "/track/1\n/x", "/track/1/\n", "//5//", "/007/x", "/1\n\n", "",
             "/track/99999999999999999999/x"]


def record_register():
    out = {"matcher": [[t, a, a3_core_register.matcher(t)(a)] for t in TEMPLATES for a in ADDRESSES],
           "collapse": [[a, a3_core_register.collapse(a)] for a in COLLAPSES]}

    rows = [{"address": "/track/1/volume", "count": 10}, {"address": "/track/2/volume", "count": 5},
            {"address": "/master/volume", "count": 2}, {"address": "/channel/0/gain", "count": 7},
            {"address": "/channel/12/gain", "count": 3}]
    out["counts_of"] = {"rows": rows, "counts": a3_core_register.counts_of(rows)}

    register = {"_comment": "c", "devices": ["core", "mixer"], "directions": ["in", "out"],
                "entries": [
                    {"address": "/channel/{ch}/gain", "device": "mixer", "direction": "in",
                     "source": "a3-mixer.py:62", "note": ""},
                    {"address": "/channel/{ch}/4d", "device": "core", "direction": "in",
                     "source": "a3-core.py:577", "note": "n"},
                    {"address": "/track/@/volume", "device": "reaper", "direction": "out",
                     "source": "x", "note": ""},
                    {"address": "/master/*", "device": "core", "direction": "in", "source": "", "note": ""}]}
    out["with_counts"] = {"register": register, "counts": out["counts_of"]["counts"],
                          "result": a3_core_register.with_counts(register, out["counts_of"]["counts"])}

    texts = {
        "good": json.dumps(register),
        "short-entries": json.dumps({"entries": [{"address": "/a"}, {"address": "/b", "device": "mixer",
                                                                   "direction": "in", "source": "x:1",
                                                                   "note": ""}]}),
        "skips": json.dumps({"entries": [{"address": ""}, {"address": None}, {"device": "x"}, "text", 5,
                                         {"address": "/ok", "extra": 1}, {"address": 7, "device": 3}],
                             "devices": ["a", 3, None, "b"], "directions": ["in"]}),
        "half": '{"entries": [{"address": "/a"',
        "wrong-shape": "[1, 2, 3]",
        "entries-not-a-list": json.dumps({"entries": {"a": 1}}),
        "no-entries": json.dumps({"devices": ["a"]}),
        "empty-file": "",
    }
    out["load"] = {}
    for name, text in texts.items():
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "osc-register.json"
            path.write_text(text)
            loaded = a3_core_register.load(path)
        out["load"][name] = {"text": text, "problem": bool(loaded["problem"]),
                             "devices": loaded["devices"], "directions": loaded["directions"],
                             "entries": loaded["entries"]}
    out["load"]["missing"] = {"text": None, "problem": True, "devices": [], "directions": [], "entries": []}

    shipped = PACKAGE / "home/aaa/.local/share/a3-core/osc-register.json"
    loaded = a3_core_register.load(shipped)
    truth = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")
    out["shipped"] = {"problem": loaded["problem"], "entries": len(loaded["entries"]),
                      "truth_addresses": len(truth.addresses()),
                      "first": loaded["entries"][0] if loaded["entries"] else None}
    return out


def main(target):
    target = Path(target)
    target.mkdir(parents=True, exist_ok=True)
    parts = {"traffic.json": {"as_json": record_as_json(), **record_bytes_and_peers()},
             "seen.json": record_seen(), "register.json": record_register()}
    shipped = PACKAGE / "home/aaa/.local/share/a3-core/osc-register.json"
    (target / "osc-register.json").write_bytes(shipped.read_bytes())
    for name, content in parts.items():
        (target / name).write_text(json.dumps(content, indent=1, ensure_ascii=True, sort_keys=True) + "\n")


if __name__ == "__main__":
    main(sys.argv[1])
