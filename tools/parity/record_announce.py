# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records what B0's a3_core_announce says, for the C++ announcement to match byte for byte:
the /core/here datagram for boot id "boot-1" on B0's shipped truth, and broadcast_address for
a table of network blocks.

    A3_CORE_BASELINE=<exported a3-core B0> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_announce.py tests/core/data/parity/announce.json
"""
import json
import os
import sys
from pathlib import Path

BASELINE = Path(os.environ["A3_CORE_BASELINE"])
PACKAGE = BASELINE / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))
os.environ.pop("A3_NETWORK", None)
os.environ.pop("A3_OSC_TRUTH", None)

import a3_osc  # noqa: E402
from a3_core_announce import announcement, broadcast_address, truth_url  # noqa: E402

BOOT_ID = "boot-1"
ADDRESSES = [
    "192.168.8.10/24", "10.0.0.5/8", "172.16.3.4/12", "192.168.8.10/25", "192.168.8.10/30",
    "192.168.8.10/31", "192.168.8.10/32", "192.168.8.10", "192.168.8.10/0",
    "192.168.8.10/255.255.255.0", "192.168.8.10/0.0.0.255", "192.168.8.10/255.255.255.252",
    "192.168.8.10/255.0.255.0", "192.168.8.10/0.0.0.0", "192.168.8.10/255.255.255.255",
    "192.168.8.10/33", "192.168.8.10/-1", "192.168.8.10/+24", "192.168.8.10/", "192.168.8.10/24/8",
    "192.168.8.300/24", "192.168.008.10/24", "192.168.8/24", "192.168.8.10.1/24",
    "192.168.8.10/24 ", " 192.168.8.10/24", "nonsense", "", "/24",
]

truth = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")
packet = announcement(truth, BOOT_ID)
result = {
    "boot_id": BOOT_ID,
    "address": truth.address("core.here"),
    "url": truth_url(truth),
    "fingerprint": truth.fingerprint(),
    "packet_hex": packet.hex(),
    "broadcast": [{"address": a, "result": broadcast_address({"address": a})} for a in ADDRESSES],
}
Path(sys.argv[1]).write_text(json.dumps(result, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
