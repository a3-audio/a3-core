"""The screencast for OBS: a listener on a port from the truth, never a hog.

The encoder runs on the machine that plays live, so it stays niced on the one
core the audio units leave free, and it never uses the GPU: an encoder hung in
a GPU reset on the rig's amdgpu on 2026-09-29.

Picture only, and as fresh as possible: on 2026-10-17 OBS showed it 0.73 s to
1.4 s late at ~11 fps. The mpegts muxer held PCR 0.73 s behind the picture,
idle scheduling on an audio core left the encoder waiting longer than it ran,
and the JACK input stalled with xruns. OBS takes the sound from AES67."""

import os
import re
import sys
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN, PACKAGE

HOME = PACKAGE / "home/aaa/.local"
SCRIPT = HOME / "bin/a3-screencast"
UNIT = HOME / "share/a3-core/config/systemd/user/a3-screencast.service"
WANTS = HOME / "share/a3-core/config/systemd/user/default.target.wants"
CONTROL = DEBIAN / "control"

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib"))
import a3_osc          # noqa: E402
import a3_osc_render   # noqa: E402

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")


def code(path):
    """The script without its comments."""
    return "\n".join(l.split("#", 1)[0] for l in path.read_text().splitlines())


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
        fanout = next(l for l in code(SCRIPT).splitlines() if "--role screencast-fanout" in l)
        self.assertIn('--port "$A3_SCREENCAST_PORT"', fanout)
        self.assertNotRegex(fanout, r"\b\d{4,5}\b")
        self.assertNotIn("listen=1", code(SCRIPT))


class OneEncoderSeveralViewers(unittest.TestCase):
    def test_ffmpeg_writes_to_stdout(self):
        line = next(l for l in code(SCRIPT).splitlines() if "-f mpegts" in l)
        self.assertTrue(line.split("\\")[0].rstrip().endswith("-f mpegts pipe:1"), line)
        self.assertNotIn("listen=1", code(SCRIPT))

    def test_the_fanout_runs_niced_on_the_free_core(self):
        text = code(SCRIPT)
        self.assertRegex(
            text, r'nice -n 19 taskset -c "\$CPUS" /usr/bin/a3-core --role screencast-fanout')
        self.assertLess(text.index("--role screencast-fanout"), text.index("ffmpeg -hide_banner"))

    def test_the_unit_says_several_viewers(self):
        description = re.search(r"^Description=(.*)$", UNIT.read_text(), re.M).group(1)
        self.assertIn("several viewers", description)


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
        self.assertNotIn("a3-screencast", (DEBIAN / "postinst").read_text())


class TheScript(unittest.TestCase):
    def test_it_is_executable(self):
        self.assertTrue(os.access(SCRIPT, os.X_OK))

    def test_it_stays_off_the_gpu(self):
        self.assertNotRegex(SCRIPT.read_text().lower(), r"(?m)^[^#]*(vaapi|nvenc|amf)")

    def test_it_yields_to_everything_else(self):
        text = SCRIPT.read_text()
        for part in ("nice -n 19", "taskset -c", "-draw_mouse 0", "xdpyinfo", "mpegts"):
            self.assertIn(part, text)

    def test_it_is_not_starved_by_idle_scheduling(self):
        self.assertNotIn("chrt", code(SCRIPT))

    def test_it_defaults_to_a_core_the_audio_units_leave_free(self):
        default = re.search(r"A3_SCREENCAST_CPUS:-([^}]+)\}", SCRIPT.read_text()).group(1)
        reaper = UNIT.with_name("a3-reaper.service").read_text()
        audio = re.search(r"^CPUAffinity=(.*)$", reaper, re.M).group(1).split()
        self.assertEqual("0", default)
        self.assertNotIn(default, audio)


class PictureOnly(unittest.TestCase):
    """OBS takes the sound from AES67; a JACK input only stalled the picture."""

    def test_it_takes_no_sound(self):
        text = code(SCRIPT)
        for part in ("-f jack", "-c:a", "aac"):
            self.assertNotIn(part, text)
        self.assertIn("-an", text.split())

    def test_nothing_promises_sound(self):
        row = TRUTH.listener("screencast", "stream")["carries"]
        description = re.search(r"^Description=(.*)$", UNIT.read_text(), re.M).group(1)
        for text in (row, description, SCRIPT.read_text().splitlines()[1]):
            self.assertNotIn("sound", text.lower())


class AsFreshAsPossible(unittest.TestCase):
    def test_the_grab_queue_is_short(self):
        sizes = [int(n) for n in re.findall(r"-thread_queue_size (\d+)", code(SCRIPT))]
        self.assertTrue(sizes)
        self.assertLessEqual(max(sizes), 8)

    def test_the_muxer_adds_no_delay(self):
        args = code(SCRIPT).split()
        for flag, value in (("-muxdelay", "0"), ("-muxpreload", "0"),
                            ("-flush_packets", "1"), ("-fflags", "nobuffer"),
                            ("-fps_mode", "vfr")):
            self.assertIn(flag, args)
            self.assertEqual(value, args[args.index(flag) + 1], flag)


class TheDependencies(unittest.TestCase):
    def test_ffmpeg_is_installed(self):
        depends = re.search(r"^Depends:(.*)$", CONTROL.read_text(), re.M).group(1)
        names = {d.strip() for d in depends.split(",")}
        self.assertIn("ffmpeg", names)


if __name__ == "__main__":
    unittest.main()
