"""The shipped patchbay wires REAPER's stereo channel meters into the analyzer.

Spec stereo-channel-meters (2026-10-06): /vu 51-66 are every channel's L and R
before and after its fader, from REAPER out51-66. Without the cables the truth
names meters that never move, and the desk -- which prefers them -- goes dark.
"""

import json
import unittest
from pathlib import Path

from tools.tests.test_package_patchbay_wires_zita import (
    PATCHBAY, sockets_and_cables, socket_client)

ROOT = Path(__file__).resolve().parents[2]
TRUTH = ROOT / "platform-config/debian-x86_64/a3-core/usr/share/a3/a3-osc.json"
FIRST_STEREO_METER = 51


def wired_pairs():
    """(REAPER output, analyzer input) for every plug a cable connects, in order."""
    outputs, inputs, cables = sockets_and_cables(PATCHBAY)
    pairs = []
    for out, into in cables:
        if socket_client(outputs, out) != "REAPER" or socket_client(inputs, into) != "beat-analyzer":
            continue
        plugs_out = [p.text for p in outputs[out].findall("plug")]
        plugs_in = [p.text for p in inputs[into].findall("plug")]
        pairs += list(zip(plugs_out, plugs_in))
    return pairs


class PatchbayWiresStereoMeters(unittest.TestCase):
    def test_every_stereo_meter_hears_its_reaper_output(self):
        meters = json.loads(TRUTH.read_text())["vu_meters"]
        pairs = set(wired_pairs())
        for number in range(FIRST_STEREO_METER, len(meters) + 1):
            name = meters[number - 1]
            with self.subTest(meter=number):
                self.assertIn((f"out{number}", f"vu_{name}"), pairs)


if __name__ == "__main__":
    unittest.main()
