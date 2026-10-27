# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records what B0's a3_osc.Truth answers, for the C++ reader to match.

    A3_CORE_BASELINE=<exported a3-core B0> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_truth.py tests/core/data/parity/truth-match.json
"""
import itertools
import json
import os
import re
import sys
from pathlib import Path

BASELINE = Path(os.environ["A3_CORE_BASELINE"])
PACKAGE = BASELINE / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))
os.environ.pop("A3_NETWORK", None)
os.environ.pop("A3_OSC_TRUTH", None)

import a3_osc  # noqa: E402

truth = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")
FIELD = re.compile(r"\{(\w+)\}")

# Not in any pattern: wrong shape, other digits (Python's \d and int() take every Nd), padding.
GARBAGE = [
    "", "/", "vu/4", "/vu", "/vu/", "/vu/4/", "/vu/ 4", "/vu/4 ", "/vu/04", "/vu/+4", "/vu/-4",
    "/vu/4.0", "/vu/1e1", "/vu/٤", "/vu/４", "/vu/\U0001d7d6", "/vu/०१",
    "/channel/１/volume", "/channel/1//volume", "/channel//volume", "/channel/1/volume/",
    "/Channel/1/volume", "/channel/1/Volume", "/channel/1/volume\n", "/channel/00001/volume",
    "/channel/99999999999999999999/volume", "/nothing", "/master", "//vu/1", "/vu/1/vu/1",
    "/channel/1/ü",
]


def probes(entry):
    fields = FIELD.findall(entry["pattern"])
    ranges = [entry[name] for name in fields]
    choices = [(low, high, high + 1) for low, high in ranges]
    seen = set()
    for combo in itertools.product(*choices) if len(fields) <= 3 else [
            tuple(c[0] for c in choices), tuple(c[1] for c in choices), tuple(c[2] for c in choices)]:
        text = entry["pattern"]
        for name, value in zip(fields, combo):
            text = text.replace("{" + name + "}", str(value))
        if text not in seen:
            seen.add(text)
            yield text


def answer(address):
    found = truth.match(address)
    return None if found is None else [found[0], found[1]]


addresses = []
for key, entry in truth.addresses().items():
    addresses.extend(probes(entry))
addresses.extend(GARBAGE)

recorded = {
    "matches": [{"address": address, "match": answer(address)} for address in addresses],
    "endpoints": [{"program": l["program"], "role": l["role"], "host": truth.endpoint(l["program"], l["role"])[0],
                   "port": truth.port(l["program"], l["role"])} for l in truth.listeners()],
    "built": [{"key": key, "address": truth.address(key, **{n: entry[n][0] for n in FIELD.findall(entry["pattern"])}),
               "ranges": {n: entry[n] for n in FIELD.findall(entry["pattern"])}}
              for key, entry in truth.addresses().items()],
    "vu_meters": truth.vu_meters(),
    "meters": list(truth.meters()),
}
Path(sys.argv[1]).write_text(json.dumps(recorded, indent=1, ensure_ascii=True) + "\n")
