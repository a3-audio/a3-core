# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Core is PTP grandmaster and steps its clients (2026-10-27).

linuxptp's stock ptp4l.conf has `step_threshold 0.0`: a client never steps
after its first lock, it only slews. After a Core reboot the Core's clock
starts at an arbitrary epoch, and a client slewed at the NIC's maximum
(6.25 %) for days. The package ships its own ptp4l config, selected by a
drop-in on linuxptp's unit; the stock /etc/linuxptp/ptp4l.conf (a linuxptp
conffile) stays untouched.

tools/tests/linuxptp-stock-ptp4l.conf is linuxptp's stock file (a3nuc1,
unedited). Every key except the three we change must equal it.
"""

import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config/debian-x86_64/a3-core"
CONF = PACKAGE / "etc/linuxptp/a3-ptp4l.conf"
DROPIN = PACKAGE / "etc/systemd/system/ptp4l@.service.d/a3.conf"
STOCK = Path(__file__).resolve().parent / "linuxptp-stock-ptp4l.conf"
CONFFILES = PACKAGE / "DEBIAN/conffiles"
POSTINST = PACKAGE / "DEBIAN/postinst"

CHANGED = {"step_threshold": "0.001", "priority1": "100", "clientOnly": "0"}


def keys(path):
    pairs = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or line.startswith("["):
            continue
        key, value = line.split(None, 1)
        pairs[key] = value.strip()
    return pairs


class TheConfig(unittest.TestCase):
    def test_it_steps_above_one_millisecond(self):
        self.assertEqual("0.001", keys(CONF)["step_threshold"])

    def test_it_is_the_grandmaster_that_wins(self):
        self.assertEqual("100", keys(CONF)["priority1"])
        self.assertEqual("0", keys(CONF)["clientOnly"])

    def test_every_other_key_is_the_stock_value(self):
        ours, stock = keys(CONF), keys(STOCK)
        self.assertEqual(set(stock), set(ours))
        for key, value in stock.items():
            if key not in CHANGED:
                self.assertEqual(value, ours[key], key)

    def test_it_has_a_global_section(self):
        self.assertIn("[global]", CONF.read_text().splitlines())

    def test_it_says_why(self):
        self.assertIn("step", CONF.read_text().split("[global]")[0])

    def test_the_stock_file_is_not_shipped(self):
        self.assertFalse((PACKAGE / "etc/linuxptp/ptp4l.conf").exists())


class TheDropIn(unittest.TestCase):
    def exec_lines(self):
        return [l.strip() for l in DROPIN.read_text().splitlines()
                if l.strip().startswith("ExecStart")]

    def test_it_clears_then_sets_exec_start(self):
        self.assertEqual(
            ["ExecStart=",
             "ExecStart=/usr/sbin/ptp4l -f /etc/linuxptp/a3-ptp4l.conf -i %I"],
            self.exec_lines())

    def test_it_is_a_service_section(self):
        self.assertIn("[Service]", DROPIN.read_text().splitlines())


class ThePackaging(unittest.TestCase):
    def test_both_files_are_conffiles(self):
        entries = CONFFILES.read_text().splitlines()
        self.assertIn("/etc/linuxptp/a3-ptp4l.conf", entries)
        self.assertIn("/etc/systemd/system/ptp4l@.service.d/a3.conf", entries)

    def test_the_stock_file_is_not_a_conffile_of_ours(self):
        self.assertNotIn("/etc/linuxptp/ptp4l.conf",
                         CONFFILES.read_text().splitlines())

    def test_the_postinst_still_enables_ptp4l_and_does_not_restart_it(self):
        text = POSTINST.read_text()
        self.assertIn('systemctl enable "ptp4l@${port}.service"', text)
        self.assertNotRegex(text, r"(restart|start)\s+[\"']?ptp4l")


if __name__ == "__main__":
    unittest.main()
