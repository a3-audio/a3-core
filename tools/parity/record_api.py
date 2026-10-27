# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records what B0's window (a3_core_web.start_window) answers over real HTTP for one scripted state, for
the C++ Core to match: /api/traffic, /api/unknown, /api/register, /api/devices, /api/truth (body and
X-A3-Truth) and the first two events of /api/stream.

    A3_CORE_BASELINE=<exported a3-core B0> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_api.py tests/core/data/parity/api

Writes api.json. The clocks of the traffic and of the window are the scripted one of record_traffic.py,
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


def main(target):
    target = Path(target)
    target.mkdir(parents=True, exist_ok=True)
    (target / "api.json").write_text(json.dumps(record(), indent=1, ensure_ascii=True, sort_keys=True) + "\n")


if __name__ == "__main__":
    main(sys.argv[1])
