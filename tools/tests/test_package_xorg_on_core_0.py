"""Xorg and x11vnc stay on CPU 0, so CPUs 1-3 are left to audio.

a3nuc1, 2026-10-17, quantum 64: CPU 1 was 87 % busy with Xorg (a VNC client
was connected), the Scarlett's USB IRQ thread and REAPER's anticipative-FX
workers; closing VNC dropped it to 40 %. The audio units run on CPUs 1-3
(CPUAffinity=1 2 3), CPU 0 is the free one. Only affinity, never
CPUScheduling* (see test_package_audio_threads_on_top).

X is started by getty autologin -> ~/.profile -> startx, so a drop-in on a
unit cannot reach it, and pinning the whole login session would drag i3 and
everything started from it onto CPU 0. startx runs ~/.xserverrc as the server:
taskset there pins Xorg alone.
"""
import configparser
import re
import unittest

from tools.tests.paths import DEBIAN, PACKAGE, ROOT

SHARE = PACKAGE / "home/aaa/.local/share/a3-core"
VNC_DROP_IN = PACKAGE / "etc/systemd/system/x11vnc.service.d/cpu.conf"
XSERVERRC = SHARE / "xserverrc"
POSTINST = DEBIAN / "postinst"
USER_UNITS = SHARE / "config/systemd/user"
AUDIO_UNITS = ("a3-reaper.service", "qjackctl.service")


def service_section(path):
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read_string(path.read_text())
    return parser["Service"]


class X11vncStaysOnCpu0(unittest.TestCase):
    def test_drop_in_pins_it_to_cpu_0(self):
        self.assertTrue(VNC_DROP_IN.is_file(), "no x11vnc.service.d/cpu.conf")
        self.assertEqual("0", service_section(VNC_DROP_IN)["CPUAffinity"])

    def test_the_drop_in_sets_no_scheduling(self):
        self.assertNotIn("CPUScheduling", VNC_DROP_IN.read_text())

    def test_the_drop_in_is_a_conffile(self):
        """Every file shipped under etc/ is one (test_package_keeps_local_config)."""
        conffiles = (DEBIAN / "conffiles").read_text()
        self.assertIn("/etc/systemd/system/x11vnc.service.d/cpu.conf", conffiles)


class XorgStaysOnCpu0(unittest.TestCase):
    def test_xserverrc_runs_the_server_under_taskset_0(self):
        self.assertTrue(XSERVERRC.is_file(), "no xserverrc shipped")
        text = XSERVERRC.read_text()
        self.assertRegex(text, r"(?m)^exec taskset -c 0 /usr/bin/X -nolisten tcp \"\$@\"$")

    def test_the_postinst_installs_it_as_the_users_xserverrc(self):
        self.assertIn(".xserverrc", POSTINST.read_text())

    def test_the_postinst_never_pins_the_login_session(self):
        self.assertNotRegex(POSTINST.read_text(), r"taskset[^\n]*(startx|i3)")


class AudioKeepsItsOwnCpus(unittest.TestCase):
    def test_audio_units_name_cpus_1_to_3_explicitly(self):
        for name in AUDIO_UNITS:
            self.assertEqual("1 2 3", service_section(USER_UNITS / name)["CPUAffinity"], name)


if __name__ == "__main__":
    unittest.main()
