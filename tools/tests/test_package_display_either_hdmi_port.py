"""The panel is rotated on whichever HDMI port it hangs on.

10-a3-display.conf named one output, HDMI-A-1. On 2026-10-15 a3nuc2's screen
moved to the other port, HDMI-A-0: X left the picture in landscape, while the
touch -- matched by device, not by port -- stayed rotated, so every touch
landed beside its target. The amdgpu driver names its two HDMI ports
HDMI-A-0 and HDMI-A-1; both get the same rotation.
"""

import re
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64" / "a3-core"
DISPLAY_CONF = PACKAGE / "etc" / "X11" / "xorg.conf.d" / "10-a3-display.conf"

HDMI_PORTS = ("HDMI-A-0", "HDMI-A-1")


def monitor_sections(text):
    """{identifier: section body} for every Monitor section."""
    sections = re.findall(r'Section\s+"Monitor"(.*?)EndSection', text, re.DOTALL)
    by_identifier = {}
    for body in sections:
        identifier = re.search(r'Identifier\s+"([^"]+)"', body)
        if identifier:
            by_identifier[identifier.group(1)] = body
    return by_identifier


class ThePanelIsRotatedOnEitherHdmiPort(unittest.TestCase):
    def test_every_hdmi_port_has_a_monitor_section(self):
        monitors = monitor_sections(DISPLAY_CONF.read_text())
        for port in HDMI_PORTS:
            with self.subTest(port=port):
                self.assertIn(port, monitors)

    def test_every_hdmi_port_turns_the_picture_like_the_touch(self):
        monitors = monitor_sections(DISPLAY_CONF.read_text())
        for port in HDMI_PORTS:
            with self.subTest(port=port):
                body = monitors.get(port, "")
                self.assertRegex(body, r'Option\s+"Rotate"\s+"right"')

    def test_every_hdmi_port_offers_the_60_hz_mode_and_prefers_it(self):
        monitors = monitor_sections(DISPLAY_CONF.read_text())
        for port in HDMI_PORTS:
            with self.subTest(port=port):
                body = monitors.get(port, "")
                modeline = re.search(
                    r'Modeline\s+"1024x768_60"\s+([\d.]+)\s+'
                    r'1024 1048 1184 (\d+)\s+768 771 777 (\d+)\s+-hsync\s+-vsync',
                    body,
                )
                self.assertIsNotNone(modeline, "no 1024x768_60 Modeline with VESA timings")
                clock_mhz, h_total, v_total = (float(g) for g in modeline.groups())
                self.assertEqual(clock_mhz, 65.0)
                self.assertEqual((h_total, v_total), (1344, 806))
                self.assertAlmostEqual(clock_mhz * 1e6 / (h_total * v_total), 60.0, places=1)
                self.assertRegex(body, r'Option\s+"PreferredMode"\s+"1024x768_60"')


if __name__ == "__main__":
    unittest.main()
