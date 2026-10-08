"""What lives outside Python is rendered from the one truth.

zita's arguments, the default network and the beat-analyzer's targets are
facts the truth already holds. Written by hand in a unit file or an .env they
would be a second truth, and the day the rig moved from 192.168.43.x to
192.168.8.x showed how a second truth ends: some copies moved, some did not.
"""

import json
import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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


class TheToolWritesTheFiles(unittest.TestCase):
    def run_tool(self, *args, env_extra=None):
        env = dict(os.environ, A3_OSC_TRUTH=str(TRUTH_FILE), **(env_extra or {}))
        return subprocess.run([sys.executable, str(TOOL), *args], env=env,
                              check=True, capture_output=True, text=True)

    def test_user_writes_zitas_file_and_the_analyzers_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self.run_tool("user", env_extra={"HOME": str(home)})
            zita = home / ".config/a3/osc.env"
            self.assertEqual(zita.read_text(), a3_osc_render.zita_env(TRUTH))
            analyzer = home / ".config/beat-analyzer/conf.d/50-a3-osc.env"
            self.assertEqual(analyzer.read_text(), a3_osc_render.analyzer_block(TRUTH))
            self.assertFalse((home / "a3-system").exists())

    def test_a_refused_network_file_is_said(self):
        # Final review 2026-10-02: the postinst would configure the interface
        # from the package's values without a word about the maintainer's file.
        with tempfile.TemporaryDirectory() as tmp:
            broken = Path(tmp) / "network.json"
            broken.write_text("{")
            done = self.run_tool("network", env_extra={"A3_NETWORK": str(broken)})
        self.assertIn("network.json", done.stderr)
        self.assertIn("refused", done.stderr)

    def test_network_prints_shell_assignments(self):
        out = self.run_tool("network").stdout
        self.assertEqual(out, a3_osc_render.network_defaults(TRUTH))

    def test_postinst_renders_for_the_user(self):
        self.assertRegex(POSTINST.read_text(), r'a3-osc-render"? user')

    def test_postinst_creates_the_network_file_as_the_user(self):
        self.assertRegex(POSTINST.read_text(), r'sudo -u "\$APPUSER" -H [^\n]*a3-osc-render"? network-file')

    def test_postinst_renders_the_users_network_as_root(self):
        self.assertRegex(POSTINST.read_text(),
                         r'(?m)A3_NETWORK="\$\{USER_HOME\}/\.config/a3/network\.json" [^\n]*a3-osc-render"? network$')



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
