"""The screencast for OBS: a listener on a port from the truth, never a hog.

The encoder runs on the machine that plays live, so it takes only what the
programs do not want (idle scheduling, one core), and it never uses the GPU:
an encoder hung in a GPU reset on the rig's amdgpu on 2026-09-29."""

import os
import re
import sys
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config/debian-x86_64/a3-core"
HOME = PACKAGE / "home/aaa/.local"
SCRIPT = HOME / "bin/a3-screencast"
UNIT = HOME / "share/a3-core/config/systemd/user/a3-screencast.service"
WANTS = HOME / "share/a3-core/config/systemd/user/default.target.wants"
CONTROL = PACKAGE / "DEBIAN/control"

sys.path.insert(0, str(HOME / "lib"))
import a3_osc          # noqa: E402
import a3_osc_render   # noqa: E402

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")


class ThePortComesFromTheTruth(unittest.TestCase):
    def test_the_truth_names_the_listener(self):
        self.assertEqual(9930, TRUTH.port("screencast", "stream"))
        self.assertEqual("any", TRUTH.listener("screencast", "stream")["host"])

    def test_the_rendered_file_carries_the_port(self):
        text = a3_osc_render.osc_env(TRUTH)
        self.assertIn("A3_SCREENCAST_PORT=9930", text.replace("'", "").replace('"', ""))

    def test_the_unit_reads_the_rendered_file(self):
        self.assertIn("EnvironmentFile=%h/" + str(a3_osc_render.ENV_FILE), UNIT.read_text())

    def test_the_script_has_no_port_number_of_its_own(self):
        listen = next(l for l in SCRIPT.read_text().splitlines() if "listen=1" in l)
        self.assertIn("${A3_SCREENCAST_PORT}", listen)
        self.assertNotRegex(listen, r"\b\d{4,5}\b")


class TheUnit(unittest.TestCase):
    def test_it_runs_the_script_and_waits_for_the_screen(self):
        text = UNIT.read_text()
        self.assertIn("ExecStart=%h/.local/bin/a3-screencast", text)
        self.assertIn("ExecStartPre=%h/.local/bin/a3-wait-for-the-screen", text)
        self.assertIn("Environment=DISPLAY=:0", text)

    def test_it_comes_back_when_the_viewer_leaves(self):
        text = UNIT.read_text()
        self.assertIn("Restart=always", text)
        self.assertIn("RestartSec=2", text)
        self.assertIn("StartLimitIntervalSec=0", text)

    def test_the_settings_file_is_optional(self):
        self.assertIn("EnvironmentFile=-%h/.config/a3/screencast.env", UNIT.read_text())

    def test_the_package_does_not_enable_it(self):
        self.assertIn("WantedBy=default.target", UNIT.read_text())  # so `enable` works
        self.assertFalse(list(WANTS.glob("a3-screencast*")))
        self.assertNotIn("a3-screencast", (PACKAGE / "DEBIAN/postinst").read_text())


class TheScript(unittest.TestCase):
    def test_it_is_executable(self):
        self.assertTrue(os.access(SCRIPT, os.X_OK))

    def test_it_stays_off_the_gpu(self):
        self.assertNotRegex(SCRIPT.read_text().lower(), r"(?m)^[^#]*(vaapi|nvenc|amf)")

    def test_it_yields_to_everything_else(self):
        text = SCRIPT.read_text()
        for part in ("nice -n 19", "chrt --idle 0", "taskset -c", "-draw_mouse 0",
                     "xdpyinfo", "-f jack", "mpegts"):
            self.assertIn(part, text)

    def test_it_connects_no_jack_port(self):
        self.assertNotIn("jack_connect", SCRIPT.read_text())

    def test_it_defaults_to_the_highest_core(self):
        self.assertIn("$(( $(nproc) - 1 ))", SCRIPT.read_text())


class TheDependencies(unittest.TestCase):
    def test_ffmpeg_is_installed(self):
        depends = re.search(r"^Depends:(.*)$", CONTROL.read_text(), re.M).group(1)
        names = {d.strip() for d in depends.split(",")}
        self.assertIn("ffmpeg", names)


if __name__ == "__main__":
    unittest.main()
