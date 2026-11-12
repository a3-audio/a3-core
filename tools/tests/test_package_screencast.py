"""The screencast for OBS: a listener on a port from the truth, never a hog.

The encoder runs on the machine that plays live, so it stays niced on the one
core the audio units leave free, and it never uses the GPU: an encoder hung in
a GPU reset on the rig's amdgpu on 2026-09-29.

Picture (optionally with Core's REC bus), and as fresh as possible: on 2026-10-17 OBS showed it 0.73 s to
1.4 s late at ~11 fps. The mpegts muxer held PCR 0.73 s behind the picture,
idle scheduling on an audio core left the encoder waiting longer than it ran,
and the JACK input stalled with xruns. The sound is optional (A3_SCREENCAST_AUDIO)."""

import os
import re
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN, DEPLOYMENT_UNITS, PACKAGE, ROOT

HOME = PACKAGE / "home/aaa/.local"
SCRIPT = HOME / "bin/a3-screencast"
UNIT = DEPLOYMENT_UNITS / "a3-screencast.service"
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
        text = code(SCRIPT)
        self.assertIn("-f mpegts)", text, "the encoder ends with the muxer, its target comes last")
        self.assertIn('"${ENCODE[@]}" pipe:1', text[text.index("--role screencast-fanout"):])
        self.assertNotIn("listen=1", code(SCRIPT))

    def test_the_fanout_runs_niced_on_the_free_core(self):
        text = code(SCRIPT)
        self.assertRegex(
            text, r'nice -n 19 taskset -c "\$CPUS" /usr/bin/a3-core --role screencast-fanout')
        self.assertIn('"${ENCODE[@]}" pipe:1', text[text.index("--role screencast-fanout"):])

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
        self.assertFalse(list(PACKAGE.rglob("*.wants/a3-screencast*")))
        self.assertNotRegex((DEBIAN / "postinst").read_text(), r"enable\s+a3-screencast")


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
        engine = (ROOT / "packaging/a3-engine.service").read_text()
        audio = re.search(r"^CPUAffinity=(.*)$", engine, re.M).group(1).split()
        self.assertEqual("0", default)
        self.assertNotIn(default, audio)


class SoundOnlyOnRequest(unittest.TestCase):
    """The default is picture only (a3nuc2 and the Pi have no engine); the sound is a setting."""

    def test_nothing_promises_sound_by_default(self):
        row = TRUTH.listener("screencast", "stream")["carries"]
        description = re.search(r"^Description=(.*)$", UNIT.read_text(), re.M).group(1)
        for text in (row, description):
            self.assertNotIn("sound", text.lower())

    def test_the_header_no_longer_says_no_sound(self):
        self.assertNotIn("No sound", SCRIPT.read_text())


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


class APushTargetSendsInsteadOfListening(unittest.TestCase):
    """2026-10-31: OBS listens on its own machine (udp://@:<port>), each rig machine sends there, so a
    restarted OBS or machine simply picks the picture up again. The target is per machine, from
    ~/.config/a3/screencast.env (A3_SCREENCAST_PUSH), never written into the script."""

    def test_the_script_reads_the_push_target(self):
        self.assertIn("A3_SCREENCAST_PUSH", code(SCRIPT))

    def test_with_a_push_target_ffmpeg_sends_there_without_the_fanout(self):
        text = code(SCRIPT)
        push = text.split('if [ -n "${A3_SCREENCAST_PUSH:-}" ]', 1)
        self.assertEqual(2, len(push), "a branch for the push target")
        branch = push[1].split("fi", 1)[0]
        self.assertIn('"$A3_SCREENCAST_PUSH"', branch)
        self.assertNotIn("screencast-fanout", branch)

    def test_no_address_is_written_into_the_script(self):
        self.assertIsNone(re.search(r"\d+\.\d+\.\d+\.\d+", code(SCRIPT)))


def print_command(**env):
    """The ffmpeg command the script would run, one argument per line."""
    full = {"PATH": os.environ["PATH"], "A3_SCREENCAST_PORT": "9930",
            "A3_SCREENCAST_SIZE": "1920x1080", "DISPLAY": ":0"}
    full.update(env)
    out = subprocess.run([str(SCRIPT), "--print-command"], env=full, check=True,
                         capture_output=True, text=True).stdout
    return out.splitlines()


def option(args, flag):
    """Every value that follows the flag, in order."""
    return [args[i + 1] for i, a in enumerate(args) if a == flag]


class WithoutSound(unittest.TestCase):
    def test_no_audio_input_and_an(self):
        args = print_command()
        self.assertEqual(1, args.count("-i"))
        self.assertIn("-an", args)
        self.assertNotIn("jack", option(args, "-f"))
        self.assertNotIn("-c:a", args)

    def test_off_when_set_to_zero(self):
        self.assertIn("-an", print_command(A3_SCREENCAST_AUDIO="0"))


class WithSound(unittest.TestCase):
    def setUp(self):
        self.args = print_command(A3_SCREENCAST_AUDIO="1")

    def test_a_jack_input_beside_the_picture(self):
        self.assertEqual(2, self.args.count("-i"))
        self.assertEqual(["x11grab", "jack", "mpegts"], option(self.args, "-f"))
        self.assertNotIn("-an", self.args)

    def test_both_inputs_run_on_the_wall_clock(self):
        self.assertEqual(["1", "1"], option(self.args, "-use_wallclock_as_timestamps"))

    def test_the_jack_client_is_a_stereo_one_with_its_own_name(self):
        i = self.args.index("jack")
        self.assertEqual(["-channels", "2"], self.args[i + 1:i + 3])
        self.assertEqual("a3-screencast", option(self.args, "-i")[1])

    def test_a_stall_does_not_shift_the_sync(self):
        self.assertTrue(option(self.args, "-af")[0].startswith("aresample=async="))

    def test_aac_in_one_mpegts(self):
        self.assertEqual(["aac"], option(self.args, "-c:a"))
        self.assertEqual(1, self.args.count("mpegts"))
        self.assertEqual("-f", self.args[-2])

    def test_the_low_latency_flags_stay(self):
        for flag, value in (("-muxdelay", "0"), ("-muxpreload", "0"), ("-flush_packets", "1")):
            self.assertEqual([value], option(self.args, flag))

    def test_the_script_links_the_rec_bus_itself(self):
        text = code(SCRIPT)
        for port in ("a3-engine:rec_L", "a3-engine:rec_R", "a3-screencast:in_1", "a3-screencast:in_2"):
            self.assertIn(port, text)


class TheVideoDelay(unittest.TestCase):
    def filters(self, **env):
        args = print_command(**env)
        return option(args, "-vf")

    def test_default_is_twenty_milliseconds(self):
        self.assertEqual(["setpts=PTS+0.020/TB"], self.filters())

    def test_the_setting_sets_it(self):
        self.assertEqual(["setpts=PTS+0.150/TB"], self.filters(A3_SCREENCAST_DELAY_MS="150"))

    def test_zero_adds_no_filter(self):
        self.assertEqual([], self.filters(A3_SCREENCAST_DELAY_MS="0"))

    def test_it_applies_without_sound_too(self):
        self.assertEqual(["setpts=PTS+0.020/TB"], self.filters(A3_SCREENCAST_AUDIO="0"))

    def test_a_non_number_is_refused(self):
        full = {"PATH": os.environ["PATH"], "A3_SCREENCAST_PORT": "9930",
                "A3_SCREENCAST_SIZE": "1x1", "A3_SCREENCAST_DELAY_MS": "soon"}
        run = subprocess.run([str(SCRIPT), "--print-command"], env=full, capture_output=True, text=True)
        self.assertNotEqual(0, run.returncode)


class WaitingForThePushTarget(unittest.TestCase):
    """A fake route check stands in for `ip route get`; it fails twice, then the target is reachable."""

    def run_wait(self, target, fails):
        with tempfile.TemporaryDirectory() as tmp:
            counter = Path(tmp) / "n"
            check = Path(tmp) / "check"
            check.write_text(
                "#!/bin/sh\n"
                f'echo "$1" >> {tmp}/asked\n'
                f'n=$(cat {counter} 2>/dev/null || echo 0); echo $((n+1)) > {counter}\n'
                f'[ "$n" -ge {fails} ]\n')
            check.chmod(0o755)
            env = {"PATH": os.environ["PATH"], "A3_SCREENCAST_PORT": "9930",
                   "A3_SCREENCAST_PUSH": target, "A3_SCREENCAST_ROUTE_CHECK": str(check),
                   "A3_SCREENCAST_WAIT_INTERVAL": "0.05"}
            start = time.monotonic()
            run = subprocess.run([str(SCRIPT), "--wait-only"], env=env, capture_output=True,
                                 text=True, timeout=20)
            asked = (Path(tmp) / "asked").read_text().split()
            return run, asked, time.monotonic() - start

    def test_it_polls_until_the_route_exists_and_logs_once(self):
        run, asked, _ = self.run_wait("udp://10.1.2.3:5000?pkt_size=1316", 3)
        self.assertEqual(0, run.returncode, run.stderr)
        self.assertEqual(4, len(asked))
        self.assertEqual({"10.1.2.3"}, set(asked))
        self.assertEqual(1, run.stderr.count("waiting"))

    def test_a_reachable_target_does_not_wait_or_log(self):
        run, asked, _ = self.run_wait("udp://10.1.2.3:5000", 0)
        self.assertEqual(1, len(asked))
        self.assertNotIn("waiting", run.stderr)

    def test_no_push_target_means_no_waiting(self):
        env = {"PATH": os.environ["PATH"], "A3_SCREENCAST_PORT": "9930"}
        run = subprocess.run([str(SCRIPT), "--wait-only"], env=env, capture_output=True, text=True, timeout=5)
        self.assertEqual(0, run.returncode)

    def test_the_unit_still_restarts_ffmpeg_dying_later(self):
        self.assertIn("Restart=always", UNIT.read_text())
