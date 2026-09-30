"""zita-n2j listens on every interface, not on the loopback.

**Why this exists.** The shipped unit started it with `localhost 65100`.
zita-n2j resolves that to `[::1]`, the IPv6 loopback, so a stream sent to
the machine from the network never arrived -- nothing complained, the JACK
ports were there and silent. Found on the rig 2026-09-29, where the unit in
~/.config had been edited to 0.0.0.0 by hand while the package still shipped
localhost for every fresh install.

The same night it listened and sounded terrible: `--buff 6` held six
milliseconds, and a sender at 256 samples and 44.1 kHz sends one packet every
5.8 ms, so any network jitter emptied the buffer -- 235 underruns in two
minutes. At 20 ms there were none.
"""

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TRUTH = ROOT / "platform-config/debian-x86_64/a3-core/usr/share/a3/a3-osc.json"
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))
import a3_osc          # noqa: E402
import a3_osc_render   # noqa: E402
UNIT = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
        / "share/a3-core/config/systemd/user/zita-n2j.service")
PATCHBAY = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
            / "share/a3-core/config/rncbc.org/a3-patchbay.xml")


def exec_start(unit):
    for line in unit.read_text().splitlines():
        if line.startswith("ExecStart="):
            return line[len("ExecStart="):].split()
    raise AssertionError(f"{unit.name} has no ExecStart")


class N2jListensOnTheNetwork(unittest.TestCase):
    def test_it_binds_every_interface(self):
        # Since 2026-09-30 the unit takes address and port from the file
        # a3-osc-render writes from the one truth.
        rendered = dict(line.split("=", 1) for line in
                        a3_osc_render.zita_env(a3_osc.load(TRUTH)).splitlines())
        address, port = rendered["A3_ZITA_N2J_HOST"], rendered["A3_ZITA_N2J_PORT"]
        self.assertEqual(address, "0.0.0.0",
                         "a loopback address hears nothing from the network")
        self.assertTrue(re.fullmatch(r"\d+", port), port)

    def test_it_buffers_more_than_one_packet_of_jitter(self):
        args = exec_start(UNIT)
        buffer_ms = int(args[args.index("--buff") + 1])
        self.assertGreaterEqual(
            buffer_ms, 20,
            "a 256-sample sender at 44.1 kHz sends every 5.8 ms; less than "
            "20 ms of buffer underran on the rig")


if __name__ == "__main__":
    unittest.main()


class N2jTakesWhatThePatchbayWires(unittest.TestCase):
    """--chan opens as many JACK outputs as it names; the patchbay wires the
    stem player's ten. Sixteen left six ports hanging (2026-09-29)."""

    def test_the_channel_count_is_the_patchbays(self):
        import xml.etree.ElementTree as ET
        args = exec_start(UNIT)
        chan = args[args.index("--chan") + 1]
        first, last = (int(n) for n in chan.split("-"))
        n2j = [s for s in ET.parse(PATCHBAY).getroot().iter("socket")
               if s.get("client").replace("\\", "") == "zita-n2j"]
        self.assertEqual(len(n2j), 1)
        self.assertEqual(last - first + 1, len(n2j[0].findall("plug")))


class ZitaComesBackByItself(unittest.TestCase):
    """zita ends on some changes to the running audio graph, and a unit that
    does not restart leaves the network audio silent until somebody notices.
    StemDeck's own units restart after 2 s (stemdeck 15e5dce); the Core's
    two do the same since 2026-09-29."""

    def test_both_units_restart_themselves(self):
        for name in ("zita-n2j.service", "zita-j2n.service"):
            unit = (UNIT.parent / name).read_text()
            self.assertRegex(unit, r"(?m)^Restart=always$", name)
            self.assertRegex(unit, r"(?m)^RestartSec=2$", name)
