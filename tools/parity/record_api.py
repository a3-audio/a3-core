# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records what B0's window (a3_core_web.start_window) answers over real HTTP for one scripted state, for
the C++ Core to match: /api/traffic, /api/unknown, /api/register, /api/devices, /api/truth (body and
X-A3-Truth) and the first two events of /api/stream.

    A3_CORE_BASELINE=<exported a3-core B0> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_api.py tests/core/data/parity/api

Writes api.json. With a second argument `bench` it writes bench.json instead -- what the same window answers for
a list arrival, /api/status and /api/send -- and the baseline must then be B1 (a3-core 7a7b2441f74e), whose window
has them:

    A3_CORE_BASELINE=<exported a3-core B1> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_api.py tests/core/data/parity/api bench

The clocks of the traffic and of the window are the scripted one of record_traffic.py,
so every number is exact; the register is the shipped file that record_traffic.py copied beside its output.
"""
import base64
import json
import os
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import record_traffic as rt  # noqa: E402  (reads A3_CORE_BASELINE)

import a3_core_devices  # noqa: E402
import a3_core_web  # noqa: E402
import a3_osc  # noqa: E402
from a3_core_traffic import Traffic  # noqa: E402

PACKAGE = rt.PACKAGE
REGISTER = PACKAGE / "home/aaa/.local/share/a3-core/osc-register.json"


class WindowClock:
    """The `time` the window sees: the scripted clock, and a sleep that hands control to the script."""
    on_sleep = None

    @staticmethod
    def monotonic():
        return rt.Scripted.mono

    @staticmethod
    def time():
        return rt.Scripted.wall

    @classmethod
    def sleep(cls, seconds):
        if cls.on_sleep is None:
            raise BrokenPipeError
        hook, cls.on_sleep = cls.on_sleep, None
        hook()


a3_core_web.time = WindowClock

FIRST = [rt.snap(90.0, "/channel/0/gain", ["float", 0.5]),
         rt.snap(91.0, "/channel/0/gain", ["float", 0.75], "mixer"),
         rt.snap(92.0, "/channel/1/pfl", ["str", "1"], "mixer"),
         rt.snap(93.0, "/channel/1/pfl", ["str", "0"], "mixer", "out"),
         rt.snap(94.0, "/odd/nan", ["float", "nan"], "motion"),
         rt.snap(94.5, "/odd/bytes", ["bytes", "6101270a00ff"], "motion"),
         rt.snap(95.0, "/grüße/♪", ["str", "grüße ♪"], "motion"),
         rt.unk(96.0, "/track/3/name", ["str", "kick"]),
         rt.unk(96.5, "/channel/3/gain", ["float", 0.25]),
         rt.unk(97.0, "/track/9/pan", ["float", "inf"])]
SECOND = [rt.snap(200.125, "/channel/0/gain", ["float", 0.875]),
          rt.snap(200.1875, "/channel/2/mute", ["bool", True], "motion")]


def get(port, path):
    with socket.create_connection(("127.0.0.1", port), timeout=5) as client:
        client.sendall(f"GET {path} HTTP/1.0\r\n\r\n".encode())
        data = b""
        while chunk := client.recv(65536):
            data += chunk
    head, _, body = data.partition(b"\r\n\r\n")
    lines = head.decode().split("\r\n")
    headers = {}
    for line in lines[1:]:
        name, _, value = line.partition(": ")
        headers[name.lower()] = value
    return {"status": int(lines[0].split()[1]), "headers": headers, "body": body}


def stream_events(port, count):
    events = []
    with socket.create_connection(("127.0.0.1", port), timeout=5) as client:
        client.sendall(b"GET /api/stream HTTP/1.0\r\n\r\n")
        data = b""
        while len(events) < count:
            data += client.recv(65536)
            head, sep, rest = data.partition(b"\r\n\r\n")
            while sep and b"\n\n" in rest and len(events) < count:
                event, _, rest = rest.partition(b"\n\n")
                events.append(json.loads(event.decode()[len("data: "):]))
            data = head + sep + rest if sep else data
    return events


def record():
    traffic = Traffic()
    rt.play(traffic, FIRST)
    devices = a3_core_devices.Devices(own_hash="abc")
    devices.heard("mixer", "old", now=0.0)
    devices.heard("motion", "abc", now=50.0)
    truth = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")
    assert a3_core_web.start_window(traffic, "127.0.0.1:0", devices=devices, register_path=REGISTER,
                                    truth=lambda: (truth.canonical(), truth.fingerprint()))
    try:
        _, port = a3_core_web.window_address()
        out = {"first_ops": FIRST, "second_ops": SECOND, "devices": [["mixer", "old", 0.0], ["motion", "abc", 50.0]],
               "own_hash": "abc", "fetched_at": 100.0, "stream_start": 200.0, "stream_next": 200.25, "responses": {}}
        rt.Scripted.mono = out["fetched_at"]
        for name in ("traffic", "unknown", "register", "devices"):
            reply = get(port, f"/api/{name}")
            assert reply["status"] == 200 and reply["headers"]["content-type"] == "application/json"
            out["responses"][name] = rt.jsonable(json.loads(reply["body"]))
        reply = get(port, "/api/truth")
        out["responses"]["truth"] = {"body_b64": base64.b64encode(reply["body"]).decode(),
                                     "header": reply["headers"]["x-a3-truth"],
                                     "content_type": reply["headers"]["content-type"]}
        reply = get(port, "/nope")
        out["responses"]["nope"] = {"status": reply["status"], "body": reply["body"].decode(),
                                    "content_type": reply["headers"]["content-type"]}

        def next_look():
            rt.play(traffic, SECOND)
            rt.Scripted.mono = out["stream_next"]

        rt.Scripted.mono = out["stream_start"]
        WindowClock.on_sleep = next_look
        out["responses"]["stream"] = [rt.jsonable(event) for event in stream_events(port, 2)]
    finally:
        a3_core_web.stop_window()
    return out


def post(port, path, body):
    """POST `body` (bytes) and return (status, parsed JSON body)."""
    with socket.create_connection(("127.0.0.1", port), timeout=5) as client:
        client.sendall(f"POST {path} HTTP/1.0\r\nContent-Length: {len(body)}\r\n\r\n".encode() + body)
        data = b""
        while chunk := client.recv(65536):
            data += chunk
    head, _, reply = data.partition(b"\r\n\r\n")
    return int(head.split()[1]), json.loads(reply)


def list_aware(spec):
    kind, raw = spec
    if kind == "list":
        return [list_aware(item) for item in raw]
    return ORIGINAL_VALUE_OF(spec)


ORIGINAL_VALUE_OF = rt.value_of
rt.value_of = list_aware

# What arrives as a Python list: several arguments, or a pointer's one. python-osc hands floats over as
# float32 widened to double, so the values here are ones float32 holds exactly or are given as such.
LIST_OPS = [rt.snap(10.0, "/stemdeck/1/pointer", ["list", [["float", 0.5], ["float", 0.25]]], "motion"),
            rt.snap(10.5, "/motion/pointer", ["list", [["float", 0.75]]], "motion"),
            rt.snap(11.0, "/odd/mixed", ["list", [["int", 3], ["str", "it's"], ["bool", True], ["NoneType", None],
                                                  ["bytes", "6101270a00ff"]]], "mixer", "out"),
            rt.snap(11.5, "/odd/quotes", ["list", [["str", "a\"b'c"], ["str", "line\nbreak\\"], ["str", "gr\u00fc\u00dfe \u266a"],
                                                  ["float", "nan"], ["float", 1e22]]], "mixer"),
            rt.snap(12.0, "/odd/empty", ["list", []], "motion"),
            rt.snap(12.5, "/odd/single", ["list", [["int", 4]]], "mixer", "out"),
            rt.unk(13.0, "/stemdeck/1/ahead", ["list", [["float", 0.5], ["str", "x"], ["int", 7]]], "motion"),
            rt.unk(13.5, "/track/3/name", ["str", "kick"])]

# (name, body as sent, which the bench's stand-in for Core's own sender is told to refuse)
SENDS = [("an int", {"to": "mixer", "address": "/channel/0/pfl", "value": "1"}),
         ("a float", {"to": "motion", "address": "/channel/0/gain", "value": "0.5"}),
         ("a negative int", {"to": "self", "address": "/channel/0/azimuth", "value": "-180"}),
         ("a quoted number", {"to": "mixer", "address": "/channel/0/pfl", "value": "\"1\""}),
         ("a bare word", {"to": "motion", "address": "/fx/mode", "value": "high_pass"}),
         ("a mistyped float", {"to": "mixer", "address": "/a", "value": "1.2.3"}),
         ("spaces around", {"to": "mixer", "address": "  /a  ", "value": "  7  "}),
         ("a number in the body", {"to": "mixer", "address": "/a", "value": 5}),
         ("nan", {"to": "mixer", "address": "/a", "value": "nan"}),
         ("exponent", {"to": "mixer", "address": "/a", "value": "1e3"}),
         ("a leading dot", {"to": "mixer", "address": "/a", "value": ".5"}),
         ("a plus sign", {"to": "mixer", "address": "/a", "value": "+3"}),
         ("an underscore", {"to": "mixer", "address": "/a", "value": "1_000"}),
         ("hex is text", {"to": "mixer", "address": "/a", "value": "0x10"}),
         ("inf", {"to": "mixer", "address": "/a", "value": "-inf"}),
         ("too large for a float", {"to": "mixer", "address": "/a", "value": "1e400"}),
         ("negative zero", {"to": "mixer", "address": "/a", "value": "-0"}),
         ("nothing typed", {"to": "mixer", "address": "/a", "value": "   "}),
         ("an empty address", {"to": "mixer", "address": "", "value": "1"}),
         ("an address without a slash", {"to": "mixer", "address": "a", "value": "1"}),
         ("no address", {"to": "mixer", "value": "1"}),
         ("no destination", {"address": "/a", "value": "1"}),
         ("no value", {"to": "mixer", "address": "/a"}),
         ("an unknown destination", {"to": "nowhere", "address": "/a", "value": "1"})]


def record_bench():
    traffic = Traffic()
    rt.play(traffic, LIST_OPS)
    sent = []

    def fake_send(to, address, value):
        if to not in ("mixer", "motion", "reaper", "self"):
            raise KeyError(to)
        sent.append([to, address, rt.jsonable(a3_core_web._jsonable(value)),
                     type(value).__name__])

    status = {"audio_engine": "engine", "reason": ""}
    assert a3_core_web.start_window(traffic, "127.0.0.1:0", send=fake_send, status=lambda: status,
                                    register_path=REGISTER)
    out = {"list_ops": LIST_OPS, "fetched_at": 100.0, "responses": {}, "sends": []}
    try:
        _, port = a3_core_web.window_address()
        rt.Scripted.mono = out["fetched_at"]
        for name in ("traffic", "unknown", "status"):
            reply = get(port, f"/api/{name}")
            assert reply["status"] == 200 and reply["headers"]["content-type"] == "application/json"
            out["responses"][name] = rt.jsonable(json.loads(reply["body"]))
        for name, body in SENDS:
            del sent[:]
            status_code, reply = post(port, "/api/send", json.dumps(body).encode())
            out["sends"].append({"name": name, "body": body, "status": status_code, "reply": rt.jsonable(reply),
                                 "sent": list(sent)})
        for name, raw in (("not json", b"not json at all"), ("a json list", b"[1, 2]")):
            del sent[:]
            status_code, reply = post(port, "/api/send", raw)
            out["sends"].append({"name": name, "raw": raw.decode(), "status": status_code, "reply": reply, "sent": list(sent)})
    finally:
        a3_core_web.stop_window()
    assert a3_core_web.start_window(Traffic(), "127.0.0.1:0")
    try:
        _, port = a3_core_web.window_address()
        out["responses"]["status_without"] = json.loads(get(port, "/api/status")["body"])
        status_code, reply = post(port, "/api/send", json.dumps(SENDS[0][1]).encode())
        out["read_only"] = {"status": status_code, "has_problem": "problem" in reply}
    finally:
        a3_core_web.stop_window()
    return out


def main(target, what="api"):
    target = Path(target)
    target.mkdir(parents=True, exist_ok=True)
    result = record_bench() if what == "bench" else record()
    (target / f"{what}.json").write_text(json.dumps(result, indent=1, ensure_ascii=True, sort_keys=True) + "\n")


if __name__ == "__main__":
    main(*sys.argv[1:])
