# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records what Python's json does, for the C++ port to match byte for byte.

    A3_CORE_BASELINE=<exported a3-core B0> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_json.py tests/core/data/parity/json.json
"""
import base64
import json
import os
import sys
from pathlib import Path

BASELINE = Path(os.environ["A3_CORE_BASELINE"])
PACKAGE = BASELINE / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

from a3_osc_join import canonical, fingerprint, join  # noqa: E402

HERE = Path(__file__).resolve().parent
FLOATS = ["0.1", "1.0", "1e-05", "1e+16", "1.5e+16", "123456789012345680.0", "-0.0",
          "5e-324", "0.30000000000000004", "1e22", "0.0001", "0.00001234", "9999999999999998.0",
          "1.7976931348623157e308", "100.0", "2.5", "-1.5e-07"]
SAMPLE_STATE = {
    "version": 2, "name": "Käse \"ü\" 😀", "zeta": [1, 2.5, None, True, False, "a/b\n\u001f\u007f"],
    "channels": {"2": {"gain": 0.5, "mute": False}, "1": {"gain": 1.0, "mute": True}},
    "empty_object": {}, "empty_list": [], "big": 123456789012345, "negative": -7,
}


def b64(data):
    return base64.b64encode(data).decode("ascii")


def entry(data):
    return {"text": json.dumps(data, ensure_ascii=False), "canonical_b64": b64(canonical(data)),
            "fingerprint": fingerprint(data)}


truth_text = (PACKAGE / "usr/share/a3/a3-osc.json").read_text(encoding="utf-8")
truth = json.loads(truth_text)
network_text = (HERE / "data/network.json").read_text(encoding="utf-8")
network = json.loads(network_text)
joined = join(truth, {key: network[key] for key in network})
network_block = {"hosts": network["hosts"], "network": network["network"]}

recorded = {
    "truth": {"text": truth_text, "canonical_b64": b64(canonical(truth)),
              "fingerprint": fingerprint(truth)},
    "network_text": network_text,
    "joined": entry(joined),
    "floats": [{"text": text, "repr": repr(json.loads(text))} for text in FLOATS],
    "sample": {"text": json.dumps(SAMPLE_STATE, ensure_ascii=False),
               "canonical_b64": b64(canonical(SAMPLE_STATE)),
               "indent2": json.dumps(SAMPLE_STATE, indent=2)},
    "network_indent1": {"text": json.dumps(network_block, ensure_ascii=False),
                        "indent1": json.dumps(network_block, indent=1)},
}
Path(sys.argv[1]).write_text(json.dumps(recorded, indent=1, ensure_ascii=True) + "\n")
