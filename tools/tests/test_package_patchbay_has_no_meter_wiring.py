# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""The shipped patchbay wires no meters into beat-analyzer any more.

Since the cut the engine measures the meters itself and beat-analyzer has no
vu_* inputs (feat/no-vu); REAPER's meter taps out31-66 fed only those. The
maintainer had the dead sockets removed (2026-10-27). The tempo input
(bpm_1, from the recording bus) stays.
"""

import unittest

from tools.tests.test_package_patchbay_wires_aes67 import (
    PATCHBAY, sockets_and_cables, socket_client)


class PatchbayHasNoMeterWiring(unittest.TestCase):
    def setUp(self):
        self.outputs, self.inputs, self.cables = sockets_and_cables(PATCHBAY)

    def test_no_analyzer_socket_takes_a_meter(self):
        for name, socket in self.inputs.items():
            if socket_client(self.inputs, name) != "beat-analyzer":
                continue
            for plug in socket.findall("plug"):
                with self.subTest(socket=name, plug=plug.text):
                    self.assertFalse(plug.text.startswith("vu_"))

    def test_no_reaper_meter_tap_is_listed(self):
        for name in self.outputs:
            with self.subTest(socket=name):
                self.assertFalse(name.startswith("reaper_vu_"))

    def test_the_tempo_input_stays_wired(self):
        self.assertIn("beat-analyzer_bpm", self.inputs)
        plugs = [p.text for p in self.inputs["beat-analyzer_bpm"].findall("plug")]
        self.assertEqual(plugs, ["bpm_1"])
        self.assertIn(("reaper_rec", "beat-analyzer_bpm"), self.cables)

    def test_every_cable_joins_two_listed_sockets(self):
        for out, into in self.cables:
            with self.subTest(cable=(out, into)):
                self.assertIn(out, self.outputs)
                self.assertIn(into, self.inputs)


if __name__ == "__main__":
    unittest.main()
