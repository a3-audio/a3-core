"""The shipped patchbay wires REAPER's analog-input taps into the analyzer.

Decided 2026-10-07 (note analog-input-meters): /vu 1-8 are the four channels'
analog inputs, L and R, from REAPER out31-38 (track "analog"). The desk's A
shows them while a stem plays, so a meter without its cable is a dark A.
"""

import json
import unittest

from tools.tests.test_package_patchbay_wires_stereo_meters import TRUTH, wired_pairs
from tools.tests.test_package_patchbay_wires_zita import PATCHBAY

FIRST_REAPER_METER_OUTPUT = 30
ANALOG_METERS = range(1, 9)


class PatchbayWiresAnalogMeters(unittest.TestCase):
    def test_every_analog_meter_hears_its_reaper_output(self):
        meters = json.loads(TRUTH.read_text())["vu_meters"]
        pairs = set(wired_pairs())
        for number in ANALOG_METERS:
            name = meters[number - 1]
            with self.subTest(meter=number):
                self.assertIn((f"out{FIRST_REAPER_METER_OUTPUT + number}", f"vu_{name}"), pairs)

    def test_the_mono_channel_plugs_are_gone(self):
        text = PATCHBAY.read_text()
        for channel in range(1, 5):
            for side in ("pre", "post"):
                with self.subTest(plug=f"vu_in{channel}_{side}"):
                    self.assertNotIn(f"<plug>vu_in{channel}_{side}</plug>", text)


if __name__ == "__main__":
    unittest.main()
