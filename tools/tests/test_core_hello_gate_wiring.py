"""The receive path asks the hello gate before it relays or remembers a
mixer value. Read off the source, like test_core_motion_wiring; the decision
is tested in test_core_hello_gate."""

import ast
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

CORE = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()


def _source_of(function_name):
    node = next(n for n in ast.walk(ast.parse(CORE))
                if isinstance(n, ast.FunctionDef) and n.name == function_name)
    return ast.get_source_segment(CORE, node)


class TheHandlersAsk(unittest.TestCase):
    def test_every_value_handler_refuses_before_it_does_anything(self):
        for name in ("osc_handler_channel", "osc_handler_master",
                     "osc_handler_filter"):
            source = _source_of(name)
            asked = source.index("values_refused(")
            self.assertLess(asked, source.index("relay("), name)
            self.assertLess(asked, source.index("traffic.seen("), name)

    def test_only_relayed_values_are_gated(self):
        """Buttons and stem turns are edges, not state; the truth's
        `relayed_to` marks the values (relay_on_arrival)."""
        self.assertIn("relay_on_arrival(", _source_of("values_refused"))

    def test_a_hello_opens_the_gate_after_the_replay(self):
        source = _source_of("osc_handler_device_hello")
        self.assertIn("_hello_gate.greet(", source)
        self.assertGreater(source.index("_hello_gate.greet("),
                           source.index("motion_said_hello("))

    def test_the_first_refusal_is_said_once(self):
        self.assertIn("values before its hello ignored",
                      _source_of("values_refused"))

    def test_cores_own_replay_comes_from_a_trusted_socket(self):
        """The evening replay is sent to Core's own port from loopback; it
        must not be refused as a stranger's values."""
        self.assertIn("_hello_gate.trust(", CORE)
        self.assertIn("_hello_gate.check(host, port)", _source_of("values_refused"))
