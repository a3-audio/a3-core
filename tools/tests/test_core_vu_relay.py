"""The meters' way to a remote Motion, as the truth says it (spec
devices-and-remote-access).

The engine sends /vu to Core's own vu-relay port, and Core forwards each
packet there to the Motion it follows -- unchanged, never parsed. A port of
its own, so 25 bundles a second never queue in front of the desk's commands
on Core's control port. The relay itself is the a3-core program's since the
cut (a3-engine); what stays here is the truth it runs on.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(ROOT / "tools/lib"))

import a3_osc   # noqa: E402
import a3_osc_render   # noqa: E402

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")


class TheTruth(unittest.TestCase):
    def test_core_has_a_vu_relay_port_of_its_own(self):
        listener = TRUTH.listener("core", "vu-relay")
        self.assertEqual(listener["host"], "local")
        self.assertNotEqual(listener["port"], TRUTH.port("core", "osc"))
        self.assertIn("/vu", listener["carries"])

    def test_the_engine_sends_its_meters_there(self):
        self.assertIn({"from": "engine", "to": "core.vu-relay",
                       "carries": "vu"}, TRUTH.routes())

    def test_core_sends_them_to_motions_vu_port(self):
        self.assertIn({"from": "core", "to": "motion.vu"}, TRUTH.routes())

    def test_the_analyzers_env_has_no_meter_line_any_more(self):
        """O13: the engine measures the meters; the analyzer gets no OSC_VU_."""
        self.assertNotIn("OSC_VU_", a3_osc_render.analyzer_block(TRUTH))


if __name__ == "__main__":
    unittest.main()
