# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""The one clock: the clock role steers the Core's PTP clock from the Scarlett (2026-10-30).

The maintainer's requirement: the Scarlett is the only clock of the A³ system,
every device follows it, and it never breaks. The program's clock role
(`a3-core --role clock`, src/a3-clock/ClockSteering.h) phase-locks the PTP
hardware clock to the Scarlett; ptp4l stays the grandmaster but never touches
that clock (free_running 1, test_package_ptp4l_steps.py), and the deck streams
are received without a resampler (test_package_aes67.py).

The role runs as the user, in the user manager beside AES67: write access to
/dev/ptp0 is the device's group, `clock` on Debian (root:clock 0664), never a
capability. The postinst puts the app user into that group.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN, ROOT

UNIT = ROOT / "packaging/a3-clock.service"
APP_STAGE = ROOT / "packaging/app/stage"
POSTINST = DEBIAN / "postinst"


def unit_value(key):
    return " ".join(re.findall(rf"(?m)^{key}=(.*)$", UNIT.read_text()))


def function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(), re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


class TheUnit(unittest.TestCase):
    def test_its_settings_are_these_and_no_others(self):
        settings = [line for line in UNIT.read_text().splitlines()
                    if line and not line.startswith("#")]
        self.assertEqual(settings, [
            "[Unit]",
            "Description=a3-clock, the Core's PTP clock steered from the Scarlett",
            "ConditionPathExists=%h/.config/pipewire/pipewire-aes67.conf",
            "After=pipewire.service wireplumber.service",
            "[Service]",
            "ExecStart=/usr/bin/a3-core --role clock",
            "Restart=always",
            "RestartSec=2",
            "[Install]",
            "WantedBy=default.target",
        ])

    def test_it_runs_the_clock_role(self):
        self.assertEqual(unit_value("ExecStart"), "/usr/bin/a3-core --role clock")

    def test_it_runs_where_aes67_runs(self):
        self.assertEqual(unit_value("ConditionPathExists"), "%h/.config/pipewire/pipewire-aes67.conf")

    def test_it_reads_the_scarlett_from_pipewire(self):
        self.assertIn("pipewire.service", unit_value("After").split())

    def test_it_comes_back_by_itself(self):
        self.assertEqual(unit_value("Restart"), "always")

    def test_it_is_not_part_of_a3_main(self):
        # Restarting the rig's programs must not take the clock down with them.
        self.assertEqual(unit_value("PartOf"), "")

    def test_it_is_started_with_the_session(self):
        self.assertEqual(unit_value("WantedBy"), "default.target")

    def test_it_takes_no_capability(self):
        text = UNIT.read_text()
        for word in ("CAP_SYS_TIME", "AmbientCapabilities", "CapabilityBoundingSet", "User=", "sudo"):
            self.assertNotIn(word, text)

    def test_the_app_ships_it(self):
        self.assertRegex(APP_STAGE.read_text(), r"for unit in [^;]*\ba3-clock\.service\b")


class AaaJoinsTheClockGroup(unittest.TestCase):
    """Driven with fake `getent`, `id` and `usermod`, never the real ones."""

    def join(self, groups, clock_group_exists=True):
        with tempfile.TemporaryDirectory() as tmp:
            bin_dir = Path(tmp)
            log = bin_dir / "usermod.log"
            (bin_dir / "id").write_text(f'#!/bin/sh\necho "{groups}"\n')
            (bin_dir / "usermod").write_text(f'#!/bin/sh\necho "$@" >> {log}\n')
            (bin_dir / "getent").write_text("#!/bin/sh\nexit 0\n" if clock_group_exists else "#!/bin/sh\nexit 2\n")
            for fake in ("id", "usermod", "getent"):
                (bin_dir / fake).chmod(0o755)
            subprocess.run(["sh", "-c", f"set -e\n{function('join_group')}\n{function('join_clock_group')}\n"
                            "join_clock_group aaa"],
                           env={"PATH": f"{bin_dir}:/usr/bin:/bin"},
                           check=True, capture_output=True, text=True)
            return log.read_text() if log.exists() else ""

    def test_a_user_outside_the_group_is_added(self):
        self.assertEqual("-aG clock aaa\n", self.join("aaa audio"))

    def test_a_member_is_left_alone(self):
        self.assertEqual("", self.join("aaa audio clock"))

    def test_no_clock_group_no_change(self):
        self.assertEqual("", self.join("aaa audio", clock_group_exists=False))

    def test_the_postinst_adds_the_app_user(self):
        self.assertRegex(POSTINST.read_text(), r'(?m)^join_clock_group "\$APPUSER"')


if __name__ == "__main__":
    unittest.main()
