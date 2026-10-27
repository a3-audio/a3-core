"""AES67 and PTP on the Core, enabled (2026-10-14).

The Core sends AES67 on its audio socket -- the second one, the truth's
network.audio_port -- and is PTP master there. Remote StemDecks play into it
and record from it. PipeWire runs the AES67 side as a second instance
(a3-aes67.service) with the config a3-osc-render renders from a template: the
socket and the sender's name are this machine's, everything else is the
package's.

What the rig settled on (2026-10-13/14), each a test below: the sender is not
in the PTP group -- the Scarlett drives it like REAPER, so the samples go out
unconverted and REAPER is never pulled under PTP; received streams are in the
PTP group with 12 ms of buffer; the PTP driver outranks every other driver;
errors only in the log, or journald throttles the real errors away under the
sender's "missing timeout" warnings.

A machine without an audio socket gets neither AES67 nor PTP.
"""

import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN, PACKAGE

LIB = Path(__file__).resolve().parents[1] / "lib"
UNITS = PACKAGE / "home/aaa/.local/share/a3-core/config/systemd/user"
UNIT = UNITS / "a3-aes67.service"
POSTINST = DEBIAN / "postinst"
TRUTH_FILE = PACKAGE / "usr/share/a3/a3-osc.json"

sys.path.insert(0, str(LIB))
import a3_osc          # noqa: E402
import a3_osc_render   # noqa: E402

TRUTH = a3_osc.load(TRUTH_FILE)


class TruthWithPort:
    """The package's truth with another audio socket."""

    def __init__(self, port):
        self._port = port

    def __getattr__(self, name):
        return getattr(TRUTH, name)

    def network(self):
        network = TRUTH.network()
        network["audio_port"] = self._port
        return network


def rendered(port="enp9s0", hostname="core-x"):
    return a3_osc_render.aes67_conf(TruthWithPort(port), hostname)


def block(text, start):
    """The `{ ... }` block that opens on the first line matching `start`."""
    begin = re.search(start, text).start()
    depth, i = 0, text.index("{", begin)
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    raise AssertionError(f"no closed block after {start}")


def settings(text):
    """Every `key = value` outside comments, in order."""
    found = []
    for line in text.splitlines():
        line = line.split("#", 1)[0]
        match = re.match(r"\s*([\w.-]+)\s*=\s*(.+?)\s*$", line)
        if match:
            found.append((match.group(1), match.group(2).strip('"')))
    return found


def value(text, key):
    values = [v for k, v in settings(text) if k == key]
    if len(values) != 1:
        raise AssertionError(f"{key} set {len(values)} times")
    return values[0]


class TheConfigIsThisMachines(unittest.TestCase):
    def test_every_socket_named_is_the_audio_port(self):
        text = rendered("enp9s0")
        named = [v for k, v in settings(text)
                 if k in ("local.ifname", "clock.interface")]
        self.assertEqual(named, ["enp9s0"] * 3)

    def test_the_sender_is_named_after_the_host(self):
        self.assertEqual(value(block(rendered(hostname="core-x"),
                                     "libpipewire-module-rtp-sink"), "sess.name"),
                         "AES67 core-x out")

    def test_nothing_is_left_unrendered(self):
        self.assertNotIn("@", rendered())

    def test_it_says_where_it_comes_from(self):
        self.assertTrue(rendered().startswith(a3_osc_render.HEADER))

    def test_no_audio_port_is_no_config(self):
        self.assertIsNone(rendered(port=""))


class TheTruthNamesTheAudioNetwork(unittest.TestCase):
    """The audio network's groups and ports are in the truth like every other
    port (decided 2026-10-14), so the ports page lists them and the config
    takes them from there instead of writing them a second time."""

    def endpoint(self, program, role):
        listener = TRUTH.listener(program, role)
        return TRUTH.host(listener["host"]), listener["port"]

    def test_the_cores_stream(self):
        self.assertEqual(("239.69.150.243", 5004), self.endpoint("aes67", "stream"))

    def test_the_announcements(self):
        self.assertEqual(("239.255.255.255", 9875), self.endpoint("aes67", "sap"))

    def test_ptp(self):
        self.assertEqual(("224.0.1.129", 319), self.endpoint("ptp4l", "event"))
        self.assertEqual(("224.0.1.129", 320), self.endpoint("ptp4l", "general"))

    def test_the_config_takes_the_stream_from_the_truth(self):
        sender = block(rendered(), "libpipewire-module-rtp-sink")
        self.assertEqual("239.69.150.243", value(sender, "destination.ip"))
        self.assertEqual("5004", value(sender, "destination.port"))

    def test_the_config_takes_the_announcements_from_the_truth(self):
        sap = block(rendered(), "libpipewire-module-rtp-sap")
        self.assertEqual("239.255.255.255", value(sap, "sap.ip"))
        self.assertEqual("9875", value(sap, "sap.port"))

    def test_the_template_writes_none_of_them(self):
        template = a3_osc_render.AES67_TEMPLATE.read_text()
        for written in ("239.69.150.243", "239.255.255.255", "5004", "9875"):
            self.assertNotIn(written, template)


class WhatTheRigSettledOn(unittest.TestCase):
    def setUp(self):
        self.text = rendered()
        self.sink = block(self.text, "libpipewire-module-rtp-sink")

    def test_twelve_channels_at_the_rigs_rate(self):
        self.assertEqual(value(self.sink, "audio.channels"), "12")
        self.assertEqual(value(self.sink, "audio.rate"), "44100")

    def test_the_sender_is_outside_the_ptp_group(self):
        self.assertNotIn("node.group", dict(settings(block(self.sink, "stream.props"))))

    def test_received_streams_are_in_the_ptp_group_with_12_ms(self):
        create = block(self.text, "create-stream")
        self.assertEqual(value(create, "node.group"), "pipewire.ptp0")
        self.assertEqual(value(create, "sess.latency.msec"), "12")

    def test_the_ptp_driver_outranks_the_others(self):
        driver = block(self.text, "factory = spa-node-factory")
        self.assertEqual(value(driver, "node.name"), "PTP0-Driver")
        self.assertEqual(value(driver, "priority.driver"), "1000")

    def test_errors_only_in_the_log(self):
        self.assertEqual(value(block(self.text, "context.properties"), "log.level"), "1")


class WritingIt(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.home = Path(self.tmp.name)
        self.conf = self.home / a3_osc_render.AES67_CONF

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, port="enp9s0"):
        return a3_osc_render.write_aes67_conf(TruthWithPort(port), self.home, "core-x")

    def test_it_is_written_into_pipewires_folder(self):
        self.write()
        self.assertEqual(self.conf.read_text(), rendered(hostname="core-x"))

    def test_a_rendered_one_is_rendered_again(self):
        self.write("enp1s0")
        self.write("enp9s0")
        self.assertIn('"enp9s0"', self.conf.read_text())

    def test_one_written_by_hand_is_kept_and_named(self):
        self.conf.parent.mkdir(parents=True)
        self.conf.write_text("# mine\n")
        said = self.write()
        self.assertEqual(self.conf.read_text(), "# mine\n")
        self.assertIn("kept", said)

    def test_without_an_audio_port_a_rendered_one_goes(self):
        self.write()
        self.write(port="")
        self.assertFalse(self.conf.exists())

    def test_without_an_audio_port_one_written_by_hand_stays(self):
        self.conf.parent.mkdir(parents=True)
        self.conf.write_text("# mine\n")
        self.write(port="")
        self.assertEqual(self.conf.read_text(), "# mine\n")

    def test_the_user_files_include_it(self):
        a3_osc_render.write_user_files(TRUTH, self.home)
        self.assertIn(f'"{TRUTH.network()["audio_port"]}"', self.conf.read_text())


def unit_value(key):
    found = re.findall(rf"(?m)^{key}=(.*)$", UNIT.read_text())
    return " ".join(found)


class TheUnit(unittest.TestCase):
    def test_it_runs_pipewire_on_the_rendered_config(self):
        self.assertEqual(unit_value("ExecStart"),
                         f"/usr/bin/pipewire -c %h/{a3_osc_render.AES67_CONF}")

    def test_it_comes_and_goes_with_pipewire(self):
        self.assertIn("pipewire.service", unit_value("Requires").split())
        after = unit_value("After").split()
        self.assertIn("pipewire.service", after)
        self.assertIn("wireplumber.service", after)

    def test_without_a_config_it_does_not_start(self):
        self.assertEqual(unit_value("ConditionPathExists"),
                         f"%h/{a3_osc_render.AES67_CONF}")

    def test_it_comes_back_by_itself(self):
        self.assertEqual(unit_value("Restart"), "always")

    def test_it_is_started_with_the_session(self):
        self.assertEqual(unit_value("WantedBy"), "default.target")


def postinst_function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(),
                     re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


class ThePostinstEnablesThem(unittest.TestCase):
    """enable_aes67 with the commands it calls stood in for, logged."""

    def run_enable(self, audio_port):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "log"
            script = (
                'render_default_network() { printf "INTERFACE=eno1\\nAUDIO_PORT=%s\\n" "$TEST_PORT"; }\n'
                f'systemctl() {{ echo "system $*" >> {log}; }}\n'
                f'user_systemctl() {{ echo "user $*" >> {log}; }}\n'
                f'{postinst_function("audio_port")}\n'
                f'{postinst_function("enable_aes67")}\n'
            )
            script += "enable_aes67\necho went-on\n"
            done = subprocess.run(["sh", "-c", f"set -e\n{script}", "sh"],
                                  env=dict(os.environ, TEST_PORT=audio_port),
                                  capture_output=True, text=True)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertIn("went-on", done.stdout)
            return (log.read_text() if log.exists() else ""), done.stdout

    def test_ptp_and_the_sender_on_the_audio_port(self):
        log, _ = self.run_enable("enp9s0")
        self.assertIn("system enable ptp4l@enp9s0.service", log)
        self.assertIn("user enable a3-aes67.service", log)

    def test_no_audio_port_enables_neither(self):
        log, said = self.run_enable("")
        self.assertNotIn("enable ptp4l", log)
        self.assertNotIn("user enable a3-aes67", log)
        self.assertIn("no audio port", said)

    def test_the_postinst_calls_it(self):
        self.assertRegex(POSTINST.read_text(), r"(?m)^\s*enable_aes67\s*$")


if __name__ == "__main__":
    unittest.main()
