# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records the OSC wire as python-osc speaks it, its Dispatcher's choices for Core's map patterns,
and Python's float()/int() of OSC argument values -- for the C++ wire, dispatcher and numbers.

    A3_CORE_BASELINE=<exported a3-core B1> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_wire.py tests/core/data/parity/wire.json

The map patterns are Core's main block at B1 (a3-core 7a7b2441f74e, a3-core.py `dispatcher.map`,
lines 2297-2342), every pattern from B1's own a3-osc.json: the families, the recall, the beat, the
device and engine hellos, one `stemdeck.deck-hello` per deck, LIVE and the two pointers. Until Task 19
the engine's words were read from a3-core's moving branch feat/plan-4b-engine-mode; B1 is fixed.
"""
import base64
import json
import os
import socket
import sys
from importlib import metadata
from pathlib import Path

BASELINE = Path(os.environ["A3_CORE_BASELINE"])
PACKAGE = BASELINE / "platform-config/debian-x86_64/a3-core"
B1 = "7a7b2441f74e377938003520e0d87ba15989f83b"
if not BASELINE.name.endswith(B1[:12]):
    sys.exit(f"A3_CORE_BASELINE is not B1's export (a3-core-{B1[:12]}): {BASELINE}")
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))
os.environ.pop("A3_NETWORK", None)
os.environ.pop("A3_OSC_TRUTH", None)

import a3_osc  # noqa: E402
from pythonosc import dispatcher as osc_dispatcher  # noqa: E402
from pythonosc import osc_packet  # noqa: E402
from pythonosc.udp_client import SimpleUDPClient  # noqa: E402

b1 = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")


def value_json(value):
    """A Python value as the C++ test rebuilds it."""
    if value is None:
        return {"py": "None"}
    if isinstance(value, bool):
        return {"py": "bool", "value": value}
    if isinstance(value, int):
        return {"py": "int", "value": str(value)}
    if isinstance(value, float):
        return {"py": "float", "hex": value.hex()}
    if isinstance(value, str):
        return {"py": "str", "utf8_b64": base64.b64encode(value.encode("utf-8", "surrogatepass")).decode()}
    if isinstance(value, bytes):
        return {"py": "bytes", "b64": base64.b64encode(value).decode()}
    if isinstance(value, tuple):
        return {"py": "tuple", "items": [value_json(v) for v in value]}
    if isinstance(value, list):
        return {"py": "list", "items": [value_json(v) for v in value]}
    raise TypeError(type(value))


# --- encode: SimpleUDPClient.send_message, the bytes on the wire -------------------------------
SENT = [
    ("/int", 1), ("/int/negative", -7), ("/int/edge-i", 2**31 - 1), ("/int/edge-i-neg", -(2**31 - 1)),
    ("/int/big", 2**31), ("/int/big-neg", -(2**31)), ("/int/huge", 2**40),
    ("/float", 0.5), ("/float/round", 1.7), ("/float/tiny", 1e-45), ("/float/nan", float("nan")),
    ("/float/inf", float("-inf")), ("/float/too-large", 1e300),
    ("/str", "text"), ("/str/empty", ""), ("/str/four", "abcd"), ("/str/utf8", "ü"),
    ("/bool/true", True), ("/bool/false", False), ("/none", None),
    ("/blob", b"\x01\x02\x03"), ("/blob/empty", b""),
    ("/list", [1, 2.5, "x", True, False]), ("/list/empty", []),
]


def sent_bytes(address, value):
    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver.bind(("127.0.0.1", 0))
    receiver.settimeout(2)
    try:
        SimpleUDPClient("127.0.0.1", receiver.getsockname()[1]).send_message(address, value)
        return {"hex": receiver.recv(65536).hex()}
    except Exception as e:  # noqa: BLE001 -- the fixture records that Python raises
        return {"raises": type(e).__name__}
    finally:
        receiver.close()


encode = [{"address": a, "value": value_json(v), **sent_bytes(a, v)} for a, v in SENT]


# --- decode: OscPacket(dgram).messages, values as Python sees them -----------------------------
def pad(b):
    return b + b"\x00" * (4 - len(b) % 4)


def message(address, tags, *payload):
    return pad(address.encode()) + (pad(tags.encode()) if tags is not None else b"") + b"".join(payload)


def bundle(*contents, timetag=b"\x00\x00\x00\x00\x00\x00\x00\x01"):
    return b"#bundle\x00" + timetag + b"".join(len(c).to_bytes(4, "big", signed=True) + c for c in contents)


I = (-5).to_bytes(4, "big", signed=True)
H = (-(2**40)).to_bytes(8, "big", signed=True)
F = bytes.fromhex("3f4ccccd")          # 0.8f
D = bytes.fromhex("3ffb333333333333")  # 1.7
BLOB = (3).to_bytes(4, "big") + b"\x01\x02\x03\x00"
POSITION = message("/channel/1/azimuth", ",f", F)

PACKETS = [
    ("int", message("/a", ",i", I)),
    ("int64", message("/a", ",h", H)),
    ("float", message("/a", ",f", F)),
    ("double", message("/a", ",d", D)),
    ("string", message("/a", ",s", pad(b"hello"))),
    ("string-four", message("/a", ",s", pad(b"abcd"))),
    ("string-empty", message("/a", ",s", pad(b""))),
    ("string-utf8", message("/a", ",s", pad("ü".encode()))),
    ("symbol-skipped", message("/a", ",Si", I)),
    ("true-false-nil", message("/a", ",TFN")),
    ("blob", message("/a", ",b", BLOB)),
    ("rgba", message("/a", ",r", bytes.fromhex("ff000080"))),
    ("midi", message("/a", ",m", bytes.fromhex("01903c7f"))),
    ("mixed", message("/channel/1/volume", ",ifsTb", I, F, pad(b"x"), BLOB)),
    ("unknown-tag-skipped", message("/a", ",ci", I)),
    ("no-type-tag", pad(b"/a")),
    ("empty-type-tag", message("/a", ",")),
    ("type-tag-without-comma", message("/a", "i", I)),
    ("no-args-after-tags", message("/a", ",i")),
    ("bundle", bundle(POSITION, message("/channel/1/elevation", ",f", F))),
    ("bundle-nested", bundle(message("/x", ",i", I), bundle(message("/y", ",i", I)), message("/z", ",i", I))),
    ("bundle-empty", bundle()),
    ("bundle-with-garbage-content", bundle(b"zzzz", message("/b", ",i", I))),
    ("bundle-future-timetag", bundle(message("/f", ",i", I), timetag=bytes.fromhex("ffffffff00000000"))),
    ("truncated-int", message("/a", ",i", I[:2])),
    ("truncated-float-padded", message("/a", ",f", F[:2])),
    ("truncated-double", message("/a", ",d", D[:4])),
    ("truncated-string", message("/a", ",s", b"hel")),
    ("truncated-string-unpadded", message("/a", ",s", b"hell\x00")),
    ("truncated-blob", message("/a", ",b", (8).to_bytes(4, "big") + b"\x01\x02")),
    ("truncated-address", b"/abc"),
    ("truncated-bundle-content", bundle(POSITION)[:-2]),
    ("truncated-bundle-size", bundle(POSITION) + b"\x00\x00"),
    ("truncated-bundle-timetag", b"#bundle\x00\x00\x00"),
    ("bad-utf8", message("/a", ",s", pad(b"\xff\xfe"))),
    ("not-osc", b"\x00\x01garbage"),
    ("no-slash", message("a", ",i", I)),
    ("empty", b""),
    ("array", message("/a", ",[ii]", I, I)),
]


def decoded(dgram):
    try:
        messages = osc_packet.OscPacket(dgram).messages
    except Exception as e:  # noqa: BLE001
        return {"raises": type(e).__name__}
    return {"messages": [{"address": m.message.address, "args": [value_json(p) for p in m.message.params]}
                         for m in messages]}


decode = [{"name": n, "hex": d.hex(), **decoded(d)} for n, d in PACKETS]


# --- the dispatcher: which of Core's maps python-osc calls, in order ----------------------------
MAPS = [
    *[{"pattern": f"/{family}/*", "from": "B1"}
      for family in ("channel", "master", "filter", "aux-return", "stemdeck")],
    {"pattern": b1.address("state.recall"), "from": "B1"},
    {"pattern": b1.address("beat"), "from": "B1"},
    {"pattern": b1.address("device.hello"), "from": "B1"},
    {"pattern": b1.address("engine.hello"), "from": "B1"},
    *[{"pattern": b1.address("stemdeck.deck-hello", deck=deck), "from": "B1"}
      for deck in range(b1.addresses()["stemdeck.deck-hello"]["deck"][0],
                        b1.addresses()["stemdeck.deck-hello"]["deck"][1] + 1)],
    {"pattern": b1.address("prodj.live"), "from": "B1"},
    {"pattern": b1.address("stemdeck.pointer"), "from": "B1"},
    {"pattern": b1.address("motion.pointer"), "from": "B1"},
]

ADDRESSES = [
    "/channel/1/volume", "/channel/4/azimuth", "/channel/1/enc", "/channel/", "/channel", "/channels/1",
    "/master/volume", "/master", "/filter/1/freq", "/aux-return/1/volume", "/aux-return",
    "/stemdeck/1/hello", "/stemdeck/2/hello", "/stemdeck/3/hello", "/stemdeck/pointer", "/motion/pointer",
    "/state/recall", "/state/recall/", "/beat", "/beat/", "/beats", "/BEAT", "/device/hello",
    "/engine/hello", "/prodj/live", "/nothing", "/", "", "/channel/1/volume\n", "/beat\n",
    "/channel/?/volume", "/b?at", "/channel/*", "/*", "/*/hello", "/stemdeck/{1,2}/hello",
    "/stemdeck/{1,9}/hello", "/stemdeck/[12]/hello", "/stemdeck/[!1]/hello", "/stemdeck/[2-9]/hello",
    "/stemdeck/[]/hello", "/stemdeck/[9-2]/hello", "/st[a-e]mdeck/pointer", "/channel/1/ü",
    "/b.at", "/be(at", "/[", "/{beat", "/beat{,}", "/de\\vice/hello",
]


def dispatched(address):
    calls = []
    disp = osc_dispatcher.Dispatcher()
    for index, entry in enumerate(MAPS):
        disp.map(entry["pattern"], lambda *_, i=index: calls.append(i))
    disp.set_default_handler(lambda *_: calls.append("default"))
    for handler in disp.handlers_for_address(address):
        handler.invoke(("127.0.0.1", 1), osc_packet.osc_message.OscMessage(message("/x", ",")))
    return calls


dispatch = [{"address": a, "calls": dispatched(a)} for a in ADDRESSES]

# --- Python's float() and int() of what a handler gets -------------------------------------------
NUMBERS = [
    "0.807429", " 1e3 ", "1_0", "nan", "-inf", "Infinity", "1.0", "0x10", "", "  ", "1", "-7", "+3", "007",
    "1__0", "_1", "1_", "1e400", "-1e400", "1e-400", "-0", ".5", "5.", ".", "1e", "e5", "inf ", "infinity",
    "-NaN", "+nan", "nan(1)", "infinit", "1,5", "1 2", "+ 1", "--1", "1_000.000_1", "1e1_0", "1_e5",
    "1._5", " 1.5 ", "١٢", "１２", "١.٥", "1.5\n", "\t-2\r",
    "\x1c3\x1f", "1\x00", "​1", "12345678901234567890", "-9223372036854775808", "9223372036854775808",
    True, False, 1.7, -1.7, 0.5, -0.0, 1e30, float("nan"), float("inf"), 2**40, -(2**31), 0,
    None, b"12", b" 1.5 ", b"1_0", b"\xa01", b"", b"nan", b"1.0",
]


def number(func, value):
    try:
        result = func(value)
    except Exception as e:  # noqa: BLE001
        return {"raises": type(e).__name__}
    return value_json(result)


numbers = [{"value": value_json(v), "float": number(float, v), "int": number(int, v)} for v in NUMBERS]

recorded = {
    "python_osc": metadata.version("python-osc"),
    "python": sys.version.split()[0],
    "a3_core": B1,
    "encode": encode,
    "decode": decode,
    "maps": MAPS,
    "dispatch": dispatch,
    "numbers": numbers,
}
Path(sys.argv[1]).write_text(json.dumps(recorded, indent=1, ensure_ascii=True) + "\n")
