"""The desk is in front: what it sent this run is laid over the evening in
every replay. Read off the source, like test_core_hello_gate_wiring; the
decision is tested in test_core_desk_first."""

import ast
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/bin/"
        "a3-core.py").read_text()


def _source_of(function_name):
    node = next(n for n in ast.walk(ast.parse(CORE))
                if isinstance(n, ast.FunctionDef) and n.name == function_name)
    return ast.get_source_segment(CORE, node)


class TheReplayCarriesTheDesk(unittest.TestCase):
    def test_every_relayed_value_is_heard_with_its_sender(self):
        source = _source_of("relay")
        heard = source.index('_desk_first.heard(address, number, origin == "mixer")')
        # Before the repeat is dropped: a desk value equal to what REAPER's
        # template reported is still the desk's.
        self.assertLess(heard, source.index("_relayed.holds("))

    def test_the_replay_lays_the_desk_over_the_evening(self):
        self.assertIn("_desk_first.over(_project.evening())",
                      _source_of("the_evening_to_replay"))
        self.assertIn("replayable(", _source_of("the_evening_to_replay"))
        self.assertIn("replay_evening(\n            the_evening_to_replay(), send_to_self)",
                      _source_of("replay_once_reaper_is_quiet"))


if __name__ == "__main__":
    unittest.main()
