# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Core's system units say how they are (2026-10-31).

The window's RIG lists every unit of the Core and shows in grey only one that
should run and does not. Three read grey although they were fine:
set_irq_prio, a oneshot at boot, read "inactive" after its run -- it keeps
RemainAfterExit now and reads "active"; vnc-display was disabled and unused
(VNC runs through x11vnc) -- it left the package, dpkg removes its file on the
upgrade (remove-on-upgrade), and the preinst disables it first if it is
enabled; a3-ptp-state is read by its timer (the window's rule, not the
package's).
"""

import configparser
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
SYSTEM = PACKAGE / "etc/systemd/system"
DEBIAN = ROOT / "packaging/DEBIAN"


def unit(path):
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read(path)
    return parser


class TheSystemUnitsReadRight(unittest.TestCase):
    def test_set_irq_prio_reads_active_after_its_run(self):
        service = unit(SYSTEM / "set_irq_prio.service")["Service"]
        self.assertEqual(service.get("Type"), "oneshot")
        self.assertEqual(service.get("RemainAfterExit"), "yes")

    def test_vnc_display_left_the_package(self):
        self.assertFalse((SYSTEM / "vnc-display.service").exists())
        self.assertFalse((PACKAGE / "home/aaa/.local/share/a3-core/recipes/vnc-display.sh").exists())
        conffiles = (DEBIAN / "conffiles").read_text().splitlines()
        self.assertNotIn("/etc/systemd/system/vnc-display.service", conffiles)
        self.assertIn("remove-on-upgrade /etc/systemd/system/vnc-display.service", conffiles)

    def test_an_enabled_vnc_display_is_disabled_before_dpkg_removes_it(self):
        preinst = (DEBIAN / "preinst").read_text()
        self.assertIn("systemctl disable vnc-display.service", preinst)
        call = preinst.index("    retire_vnc_display\n")
        self.assertGreater(call, preinst.index("install|upgrade)"))


if __name__ == "__main__":
    unittest.main()
