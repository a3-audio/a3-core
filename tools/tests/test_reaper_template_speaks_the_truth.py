"""The IEM plug-ins in the REAPER template listen and send where the truth says.

Their OSC ports are not in any file of ours: each plug-in keeps them in its
own state, which REAPER writes into the template as base64 (`<OSCConfig
ReceiverPort=...>`). So they were the one place a port could drift from
a3-osc.json without anything reading it -- Core would send the azimuths to
1337 while the encoder listened on another number, and nothing would say so.
"""

import base64
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc  # noqa: E402

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")
TEMPLATE = PACKAGE / "home/aaa/.local/share/a3-core/config/REAPER/ProjectTemplates/a3-reaper.RPP"

_PLUGIN = re.compile(r'<VST "VST3: (\w+) \(IEM\)"')
_BASE64_LINE = re.compile(r"\s*[A-Za-z0-9+/=]+")
_OSC_CONFIG = re.compile(rb"<OSCConfig ([^>]*)/>")
_ATTRIBUTE = re.compile(rb'(\w+)="([^"]*)"')


def osc_configs(path):
    """(plug-in, {attribute: value}) for every IEM plug-in's OSC settings.

    REAPER writes a plug-in's state as lines of base64, each padded on its
    own -- decoded line by line, not as one string."""
    lines = path.read_text(errors="replace").splitlines()
    found = []
    for i, line in enumerate(lines):
        plugin = _PLUGIN.search(line)
        if not plugin:
            continue
        raw = b""
        for chunk in lines[i + 1:]:
            if not _BASE64_LINE.fullmatch(chunk):
                break
            raw += base64.b64decode(chunk.strip())
        for config in _OSC_CONFIG.findall(raw):
            found.append((plugin.group(1), {k.decode(): v.decode()
                                            for k, v in _ATTRIBUTE.findall(config)}))
    return found


class TheEncodersListenWhereCoreSends(unittest.TestCase):
    def test_the_three_multi_encoders(self):
        heard = sorted(int(c["ReceiverPort"]) for name, c in osc_configs(TEMPLATE)
                       if name == "MultiEncoder")
        said = sorted(TRUTH.port("iem", f"multiencoder-{n}") for n in (1, 2, 3))
        self.assertEqual(heard, said)

    def test_the_dual_delay(self):
        heard = [int(c["ReceiverPort"]) for name, c in osc_configs(TEMPLATE)
                 if name == "DualDelay"]
        self.assertEqual(heard, [TRUTH.port("dualdelay", "osc")])


class TheEnergyGoesToMotion(unittest.TestCase):
    def test_the_visualizer_sends_where_motion_listens(self):
        sent = [(c["SenderIP"], int(c["SenderPort"])) for name, c in osc_configs(TEMPLATE)
                if name == "EnergyVisualizer"]
        self.assertEqual(sent, [TRUTH.endpoint("motion", "energy")])


if __name__ == "__main__":
    unittest.main()
