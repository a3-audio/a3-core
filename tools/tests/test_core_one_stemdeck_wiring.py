"""Core follows one StemDeck and says so loudly when a second one is there.

Read off the source, like test_core_motion_wiring: Core cannot be started in
a test. The decision is tested in test_core_presence (OnlyOneMayRun); this
holds that a3-core.py asks for it and speaks the answer.
"""

import ast
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
CORE = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()


def _source_of(function_name):
    node = next(n for n in ast.walk(ast.parse(CORE))
                if isinstance(n, ast.FunctionDef) and n.name == function_name)
    return ast.get_source_segment(CORE, node)


class TwoStemDecks(unittest.TestCase):
    def test_the_watch_follows_one_at_a_time(self):
        start = CORE.index("_stemdeck_watch = HelloWatch(")
        self.assertIn("one_at_a_time=True", CORE[start:start + 250])

    def test_a_rival_is_said_in_the_journal(self):
        source = _source_of("stemdeck_said_hello")
        self.assertIn("take_rivals()", source)
        self.assertIn("a second StemDeck", source)
        self.assertIn("only one may run", source)

    def test_the_rival_is_named_before_the_early_return(self):
        """A rival's hello is not news for the link, so the early return of
        stemdeck_said_hello must not come before it is said."""
        source = _source_of("stemdeck_said_hello")
        self.assertLess(source.index("take_rivals()"), source.index("return"))
        self.assertLess(source.index("_stemdeck_watch.hello("),
                        source.index("take_rivals()"))

    def test_it_names_the_host_followed(self):
        self.assertIn("_stemdeck_watch.host", _source_of("stemdeck_said_hello"))
