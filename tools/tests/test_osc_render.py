"""What lives outside Python is rendered from the one truth.

x11vnc's port, the default network and the beat-analyzer's targets are
facts the truth already holds. Written by hand in a unit file or an .env they
would be a second truth, and the day the rig moved from 192.168.43.x to
192.168.8.x showed how a second truth ends: some copies moved, some did not.
"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.tests.paths import DEBIAN, DEPLOYMENT_UNITS, PACKAGE

LIB = Path(__file__).resolve().parents[1] / "lib"
TRUTH_FILE = PACKAGE / "usr/share/a3/a3-osc.json"
UNITS = DEPLOYMENT_UNITS
POSTINST = DEBIAN / "postinst"
TEMPLATES = DEBIAN / "templates"

sys.path.insert(0, str(LIB))
import a3_osc          # noqa: E402
import a3_osc_render   # noqa: E402

TRUTH = a3_osc.load(TRUTH_FILE)


def pairs(text):
    """KEY=value lines, comments and blanks left out."""
    found = {}
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            found[key] = value
    return found


def exec_start(unit):
    for line in (UNITS / unit).read_text().splitlines():
        if line.startswith("ExecStart="):
            return line[len("ExecStart="):]
    raise AssertionError(f"{unit} has no ExecStart")


class NoZitaIsRendered(unittest.TestCase):
    """zita is gone from the Core (2026-10-14): AES67 carries the network
    audio, and osc.env names no bridge any more."""

    def test_the_rendered_file_names_no_zita(self):
        self.assertNotIn("ZITA", a3_osc_render.osc_env(TRUTH))


SYSTEM_UNITS = PACKAGE / "etc/systemd/system"


class VncIsRendered(unittest.TestCase):
    """x11vnc's port and the viewer's address were written beside the truth
    (a3-core#63)."""

    def test_the_truth_names_vncs_listener(self):
        listener = TRUTH.listener("x11vnc", "vnc")
        self.assertEqual("any", listener["host"])

    def test_the_rendered_file_carries_vncs_port(self):
        env = pairs(a3_osc_render.osc_env(TRUTH))
        self.assertEqual(int(env["A3_VNC_PORT"]), TRUTH.port("x11vnc", "vnc"))

    def test_the_unit_takes_its_port_from_the_rendered_file(self):
        text = (SYSTEM_UNITS / "x11vnc.service").read_text()
        # A system unit's %h is root's home, whatever User= says.
        self.assertIn("EnvironmentFile=/home/aaa/" + str(a3_osc_render.ENV_FILE), text)
        start = next(line for line in text.splitlines() if line.startswith("ExecStart="))
        self.assertIn("-rfbport ${A3_VNC_PORT}", start)
        self.assertNotRegex(start, r"\b\d{4,5}\b")


class TheNetworkDefaultsAreRendered(unittest.TestCase):
    def test_the_default_network_is_the_truths(self):
        shell = pairs(a3_osc_render.network_defaults(TRUTH))
        network = TRUTH.network()
        self.assertEqual(shell["INTERFACE"], network["interface"])
        self.assertEqual(shell["ADDR"], network["address"])
        self.assertEqual(shell["GATEWAY"], network["gateway"])
        self.assertEqual(shell["DNS"], network["dns"])

    def test_the_default_puts_the_audio_network_on_the_other_port(self):
        shell = pairs(a3_osc_render.network_defaults(TRUTH))
        self.assertEqual(shell["AUDIO_PORT"], TRUTH.network()["audio_port"])
        self.assertEqual(shell["AUDIO_ADDR"], TRUTH.network()["audio_address"])
        self.assertNotIn("BRIDGE_WITH", shell)

    def test_postinst_takes_the_default_from_the_tool(self):
        text = POSTINST.read_text()
        self.assertRegex(text, r'/usr/bin/a3-core --render-truth network')
        self.assertNotIn('ADDR="192.168.', text)

    def test_the_questions_default_to_the_truth(self):
        """debconf reads its defaults from the static templates file, so they
        cannot be rendered -- they are held against the truth instead."""
        defaults, question = {}, None
        for line in TEMPLATES.read_text().splitlines():
            if line.startswith("Template: "):
                question = line.split(": ", 1)[1]
            elif line.startswith("Default:"):
                defaults[question] = line.split(":", 1)[1].strip()
        network = TRUTH.network()
        self.assertEqual(defaults["a3-core/interface"], network["interface"])
        self.assertEqual(defaults["a3-core/address"], network["address"])
        self.assertEqual(defaults["a3-core/gateway"], network["gateway"])
        self.assertEqual(defaults["a3-core/dns"], network["dns"])
        self.assertEqual(defaults["a3-core/audio-address"], network["audio_address"])


class TheAnalyzerTargetsAreRendered(unittest.TestCase):
    def test_every_route_from_the_analyzer_is_a_target(self):
        targets = pairs(a3_osc_render.analyzer_block(TRUTH))
        for route in TRUTH.routes():
            if route.get("from") != "beat-analyzer":
                continue
            program, role = route["to"].split(".")
            host, port = TRUTH.endpoint(program, role)
            with self.subTest(route=route["to"]):
                self.assertEqual(targets["OSC_HOST_" + program], f"{host}:{port}")

    def test_the_analyzer_gets_no_meter_target(self):
        """O13: the engine measures the meters and Core sends them on, so the
        analyzer's conf.d carries clock targets only (no OSC_VU_ line)."""
        targets = pairs(a3_osc_render.analyzer_block(TRUTH))
        self.assertEqual([k for k in targets if k.startswith("OSC_VU_")], [])

    def test_a_meter_route_of_the_analyzers_renders_no_line(self):
        """The analyzer has no meter code left: a truth that still routes /vu
        from it (an older one, a hand edit) gets no OSC_VU_ line and no
        second target for that program."""
        data = TRUTH.data()
        data["routes"] += [{"from": "beat-analyzer", "to": "motion.vu"},
                           {"from": "beat-analyzer", "to": "core.vu-relay",
                            "carries": "vu"}]
        block = a3_osc_render.analyzer_block(a3_osc.Truth(data))
        self.assertNotIn("OSC_VU_", block)
        self.assertEqual(block, a3_osc_render.analyzer_block(TRUTH))

    def test_the_analyzer_gets_no_meter_address(self):
        """OSC_ADDRESS_VU went with the analyzer's meters."""
        self.assertNotIn("OSC_ADDRESS_VU", a3_osc_render.analyzer_block(TRUTH))

    def test_nothing_else_is_a_target(self):
        targets = pairs(a3_osc_render.analyzer_block(TRUTH))
        routed = [route for route in TRUTH.routes()
                  if route.get("from") == "beat-analyzer"]
        sent_to = [k for k in targets if k.startswith("OSC_HOST_")]
        self.assertEqual(len(sent_to), len(routed))

    def test_the_analyzer_speaks_the_truths_words(self):
        """Its addresses and the Pro DJ Link ports, too (beat-analyzer's
        Config::OscWords reads them from the block)."""
        targets = pairs(a3_osc_render.analyzer_block(TRUTH))
        self.assertEqual(targets["OSC_ADDRESS_BEAT"], TRUTH.pattern("beat"))
        self.assertEqual(targets["OSC_ADDRESS_TAP"], TRUTH.pattern("tap"))
        self.assertEqual(targets["OSC_ADDRESS_CLOCKMODE"], TRUTH.pattern("clockmode"))
        self.assertEqual(int(targets["PIONEER_PORT_ANNOUNCE"]), TRUTH.port("prolink", "announce"))
        self.assertEqual(int(targets["PIONEER_PORT_BEAT"]), TRUTH.port("prolink", "beat"))
        self.assertEqual(int(targets["PIONEER_PORT_STATUS"]), TRUTH.port("prolink", "status"))

    def test_the_analyzer_listens_where_the_truth_says(self):
        targets = pairs(a3_osc_render.analyzer_block(TRUTH))
        self.assertEqual(int(targets["OSC_PORT_A3MOTION"]),
                         TRUTH.port("beat-analyzer", "clock"))


class TheAnalyzerGetsAFileOfItsOwn(unittest.TestCase):
    """The beat-analyzer is its own package and reads conf.d/*.env after the
    user's file (2026-10-08). a3-core owns one whole file there, so nothing
    is spliced into a file somebody edits, and the truth wins key by key."""

    def test_the_file_sits_in_the_analyzers_conf_d(self):
        self.assertEqual(a3_osc_render.ANALYZER_CONF,
                         Path(".config/beat-analyzer/conf.d/50-a3-osc.env"))

    def test_the_file_says_whose_it_is(self):
        first = a3_osc_render.analyzer_block(TRUTH).splitlines()[0]
        self.assertTrue(first.startswith("#"))
        self.assertIn("a3-osc-render", first)

    def test_written_whole_and_the_same_again(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            a3_osc_render.write_user_files(TRUTH, home)
            conf = home / a3_osc_render.ANALYZER_CONF
            once = conf.read_text()
            self.assertEqual(once, a3_osc_render.analyzer_block(TRUTH))
            conf.write_text(once.replace("=", "=stale", 1))
            a3_osc_render.write_user_files(TRUTH, home)
            self.assertEqual(conf.read_text(), once)

    def test_a_reader_never_sees_an_empty_file(self):
        """Core rewrites the file at every start while the analyzer may be
        reading it: the new file is swapped in whole, never emptied first."""
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            conf = home / a3_osc_render.ANALYZER_CONF
            conf.parent.mkdir(parents=True)
            conf.write_text("OLD=1\n")
            with conf.open() as reader:
                a3_osc_render.write_user_files(TRUTH, home)
                self.assertEqual(reader.read(), "OLD=1\n")
            self.assertEqual(conf.read_text(), a3_osc_render.analyzer_block(TRUTH))

    def test_no_temporary_file_is_left_for_the_analyzer_to_read(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            a3_osc_render.write_user_files(TRUTH, home)
            names = sorted(p.name for p in (home / a3_osc_render.ANALYZER_CONF).parent.iterdir())
            self.assertEqual(names, ["50-a3-osc.env"])

    def test_the_old_checkout_env_is_left_alone(self):
        """The package never reads build/.env; the rig's copy is carried over
        by hand (smoke-test/beat-analyzer-deb.md), not rewritten here."""
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            old = home / "a3-system/beat-analyzer/build/.env"
            old.parent.mkdir(parents=True)
            old.write_text("LOG_LEVEL=1\n")
            a3_osc_render.write_user_files(TRUTH, home)
            self.assertEqual(old.read_text(), "LOG_LEVEL=1\n")


class TheNetworkFile(unittest.TestCase):
    """Spec truth-from-core: ~/.config/a3/network.json, created once."""

    def test_it_holds_the_two_network_blocks(self):
        data = json.loads(a3_osc_render.network_file(TRUTH))
        self.assertEqual(set(data), {"hosts", "network"})
        self.assertEqual(data["hosts"]["mixer"], TRUTH.host("mixer"))

    def test_it_is_created_once_and_never_overwritten(self):
        path = Path(tempfile.mkdtemp()) / "a3" / "network.json"
        self.assertTrue(a3_osc_render.write_network_file_once(TRUTH, path))
        path.write_text('{"mine": true}')
        self.assertFalse(a3_osc_render.write_network_file_once(TRUTH, path))
        self.assertEqual(path.read_text(), '{"mine": true}')

    def test_the_network_render_reads_the_named_file(self):
        # The postinst renders Core's interface as root; A3_NETWORK points at
        # the user's file, so root's home is never read.
        path = Path(tempfile.mkdtemp()) / "network.json"
        data = json.loads(a3_osc_render.network_file(TRUTH))
        data["network"]["address"] = "10.1.2.3/24"
        path.write_text(json.dumps(data))
        with mock.patch.dict(os.environ, {"A3_NETWORK": str(path)}):
            rendered = a3_osc_render.network_defaults(a3_osc.load(TRUTH_FILE))
        self.assertIn("10.1.2.3/24", rendered)



if __name__ == "__main__":
    unittest.main()
