"""What lives outside Python is rendered from the one truth.

zita's arguments, the default network and the beat-analyzer's targets are
facts the truth already holds. Written by hand in a unit file or an .env they
would be a second truth, and the day the rig moved from 192.168.43.x to
192.168.8.x showed how a second truth ends: some copies moved, some did not.
"""

import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64" / "a3-core"
LIB = PACKAGE / "home/aaa/.local/lib"
TOOL = PACKAGE / "home/aaa/.local/bin/a3-osc-render"
TRUTH_FILE = PACKAGE / "usr/share/a3/a3-osc.json"
UNITS = PACKAGE / "home/aaa/.local/share/a3-core/config/systemd/user"
POSTINST = PACKAGE / "DEBIAN/postinst"
TEMPLATES = PACKAGE / "DEBIAN/templates"

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


class ZitaIsRendered(unittest.TestCase):
    def test_j2n_sends_where_the_truth_routes_it(self):
        env = pairs(a3_osc_render.zita_env(TRUTH))
        self.assertEqual((env["A3_ZITA_J2N_HOST"], int(env["A3_ZITA_J2N_PORT"])),
                         TRUTH.endpoint("radla", "zita-n2j"))

    def test_n2j_listens_where_the_truth_says(self):
        env = pairs(a3_osc_render.zita_env(TRUTH))
        listener = TRUTH.listener("zita-n2j", "audio")
        self.assertEqual(env["A3_ZITA_N2J_HOST"], TRUTH.host(listener["host"]))
        self.assertEqual(int(env["A3_ZITA_N2J_PORT"]), listener["port"])

    def test_the_units_carry_no_address_of_their_own(self):
        for unit in ("zita-j2n.service", "zita-n2j.service"):
            with self.subTest(unit=unit):
                line = exec_start(unit)
                self.assertNotRegex(line, r"\d+\.\d+\.\d+\.\d+")
                self.assertNotRegex(line, r"\b\d{4,5}\b")
                self.assertIn("${A3_ZITA_", line)

    def test_the_units_read_the_rendered_file(self):
        for unit in ("zita-j2n.service", "zita-n2j.service"):
            with self.subTest(unit=unit):
                text = (UNITS / unit).read_text()
                self.assertIn(f"EnvironmentFile={a3_osc_render.SYSTEMD_ENV_FILE}", text)


class TheNetworkDefaultsAreRendered(unittest.TestCase):
    def test_the_default_network_is_the_truths(self):
        shell = pairs(a3_osc_render.network_defaults(TRUTH))
        network = TRUTH.network()
        self.assertEqual(shell["INTERFACE"], network["interface"])
        self.assertEqual(shell["ADDR"], network["address"])
        self.assertEqual(shell["GATEWAY"], network["gateway"])
        self.assertEqual(shell["DNS"], network["dns"])

    def test_the_default_bridges_the_other_port(self):
        shell = pairs(a3_osc_render.network_defaults(TRUTH))
        others = [port for port in TRUTH.network()["bridge_ports"]
                  if port != TRUTH.network()["interface"]]
        self.assertEqual(shell["BRIDGE_WITH"], shlex.quote(" ".join(others)))

    def test_postinst_takes_the_default_from_the_tool(self):
        text = POSTINST.read_text()
        self.assertRegex(text, r'a3-osc-render"? network')
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


class TheAnalyzerTargetsAreRendered(unittest.TestCase):
    def test_every_route_from_the_analyzer_is_a_target(self):
        targets = pairs(a3_osc_render.analyzer_block(TRUTH))
        for route in TRUTH.routes():
            if route.get("from") != "beat-analyzer":
                continue
            program, role = route["to"].split(".")
            vu = role == "vu" or route.get("carries") == "vu"
            prefix = "OSC_VU_" if vu else "OSC_HOST_"
            host, port = TRUTH.endpoint(program, role)
            with self.subTest(route=route["to"], vu=vu):
                self.assertEqual(targets[prefix + program], f"{host}:{port}")

    def test_the_mixer_gets_its_meters(self):
        """Once one OSC_VU_ target exists the analyzer sends /vu to those
        only -- a program on one port for both needs its OSC_VU_ line too, or
        its meters stop (the mixer, whose meters sit on its OSC port)."""
        targets = pairs(a3_osc_render.analyzer_block(TRUTH))
        host, port = TRUTH.endpoint("mixer", "osc")
        self.assertEqual(targets["OSC_VU_mixer"], f"{host}:{port}")

    def test_nothing_else_is_a_target(self):
        targets = pairs(a3_osc_render.analyzer_block(TRUTH))
        routed = [route for route in TRUTH.routes()
                  if route.get("from") == "beat-analyzer"]
        sent_to = [k for k in targets if k.startswith(("OSC_HOST_", "OSC_VU_"))]
        self.assertEqual(len(sent_to), len(routed))

    def test_the_analyzer_speaks_the_truths_words(self):
        """Its addresses and the Pro DJ Link ports, too (beat-analyzer's
        Config::OscWords reads them from the block)."""
        targets = pairs(a3_osc_render.analyzer_block(TRUTH))
        self.assertEqual(targets["OSC_ADDRESS_BEAT"], TRUTH.pattern("beat"))
        self.assertEqual(targets["OSC_ADDRESS_TAP"], TRUTH.pattern("tap"))
        self.assertEqual(targets["OSC_ADDRESS_CLOCKMODE"], TRUTH.pattern("clockmode"))
        self.assertEqual(targets["OSC_ADDRESS_VU"], TRUTH.pattern("vu"))
        self.assertEqual(int(targets["PIONEER_PORT_ANNOUNCE"]), TRUTH.port("prolink", "announce"))
        self.assertEqual(int(targets["PIONEER_PORT_BEAT"]), TRUTH.port("prolink", "beat"))
        self.assertEqual(int(targets["PIONEER_PORT_STATUS"]), TRUTH.port("prolink", "status"))

    def test_the_analyzer_listens_where_the_truth_says(self):
        targets = pairs(a3_osc_render.analyzer_block(TRUTH))
        self.assertEqual(int(targets["OSC_PORT_A3MOTION"]),
                         TRUTH.port("beat-analyzer", "clock"))


class TheAnalyzerEnvKeepsTheRest(unittest.TestCase):
    MAINTAINERS = ("JACK_CLIENT_NAME=beat-analyzer\n"
                   "OSC_HOST_core=127.0.0.1:9000\n"
                   "OSC_VU_motion=127.0.0.1:7772\n"
                   "OSC_SEND_RATE=25\n"
                   "OSC_PORT_A3MOTION=7775\n"
                   "LOG_LEVEL=1\n")

    def put(self, text):
        return a3_osc_render.put_analyzer_block(text, a3_osc_render.analyzer_block(TRUTH))

    def test_the_first_render_retires_the_hand_written_targets(self):
        rendered = self.put(self.MAINTAINERS)
        outside = rendered.split(a3_osc_render.BEGIN)[0]
        self.assertNotIn("\nOSC_HOST_core=", "\n" + outside)
        self.assertNotIn("\nOSC_VU_motion=", "\n" + outside)
        self.assertNotIn("\nOSC_PORT_A3MOTION=", "\n" + outside)
        self.assertIn("# was: OSC_HOST_core=127.0.0.1:9000", outside)

    def test_the_rest_stays_as_it_was(self):
        rendered = self.put(self.MAINTAINERS)
        for line in ("JACK_CLIENT_NAME=beat-analyzer", "OSC_SEND_RATE=25", "LOG_LEVEL=1"):
            self.assertIn(line + "\n", rendered)

    def test_a_second_render_changes_nothing(self):
        once = self.put(self.MAINTAINERS)
        self.assertEqual(self.put(once), once)

    def test_only_the_block_is_replaced(self):
        once = self.put(self.MAINTAINERS)
        stale = once.replace("127.0.0.1:9000", "10.0.0.1:1")
        edited = stale.replace("LOG_LEVEL=1", "LOG_LEVEL=0")
        again = self.put(edited)
        self.assertIn("LOG_LEVEL=0\n", again)
        self.assertIn("OSC_HOST_core=127.0.0.1:9000", again)
        self.assertNotIn("10.0.0.1:1", again.split(a3_osc_render.BEGIN)[1])


class TheToolWritesTheFiles(unittest.TestCase):
    def run_tool(self, *args, env_extra=None):
        env = dict(os.environ, A3_OSC_TRUTH=str(TRUTH_FILE), **(env_extra or {}))
        return subprocess.run([sys.executable, str(TOOL), *args], env=env,
                              check=True, capture_output=True, text=True)

    def test_user_writes_zitas_file_and_the_analyzers_block(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            analyzer_env = home / "a3-system/beat-analyzer/build/.env"
            analyzer_env.parent.mkdir(parents=True)
            analyzer_env.write_text(TheAnalyzerEnvKeepsTheRest.MAINTAINERS)
            self.run_tool("user", env_extra={"HOME": str(home)})
            zita = home / ".config/a3/osc.env"
            self.assertEqual(zita.read_text(), a3_osc_render.zita_env(TRUTH))
            self.assertIn(a3_osc_render.BEGIN, analyzer_env.read_text())

    def test_user_leaves_a_missing_analyzer_alone(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self.run_tool("user", env_extra={"HOME": str(home)})
            self.assertFalse((home / "a3-system").exists())

    def test_network_prints_shell_assignments(self):
        out = self.run_tool("network").stdout
        self.assertEqual(out, a3_osc_render.network_defaults(TRUTH))

    def test_postinst_renders_for_the_user(self):
        self.assertRegex(POSTINST.read_text(), r'a3-osc-render"? user')


if __name__ == "__main__":
    unittest.main()
