"""A channel value Core handles is not also written down as unknown.

osc_handler_channel decides what a parameter does in three `if` chains one
after the other -- the fader and the desk's knobs, then Motion's position and
filter -- and the last chain ends in `else: traffic.unknown(...)`. That `else`
belonged to the last chain only, so a volume the middle chain had just sent to
REAPER fell through into it as well: every fader, gain, EQ, send and cue key
showed in the window as an address nobody serves (seen 2026-10-01, a fader on
the desk listed as unknown seconds after it moved). One chain, one `else`.

Read off the source like test_core_handlers_are_mapped: Core itself cannot be
started in a test.
"""

import ast
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
CORE = PACKAGE / "home/aaa/.local/bin/a3-core.py"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc  # noqa: E402

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")


def handler():
    tree = ast.parse(CORE.read_text())
    return next(node for node in ast.walk(tree)
                if isinstance(node, ast.FunctionDef) and node.name == "osc_handler_channel")


def tests_parameter(node):
    test = node.test
    return (isinstance(test, ast.Compare) and isinstance(test.left, ast.Name)
            and test.left.id == "parameter")


def chain_parameters(node):
    """The parameters an if/elif chain compares against, in order."""
    names = []
    while isinstance(node, ast.If) and tests_parameter(node):
        names.append(node.test.comparators[0].value)
        node = node.orelse[0] if len(node.orelse) == 1 else None
    return names


class OneChainOneElse(unittest.TestCase):
    def test_the_parameter_is_decided_in_one_chain(self):
        chains = [node for node in handler().body
                  if isinstance(node, ast.If) and tests_parameter(node)]
        self.assertEqual(len(chains), 1,
                         [chain_parameters(c) for c in chains])

    def test_every_channel_word_core_hears_has_its_branch(self):
        chains = [node for node in handler().body
                  if isinstance(node, ast.If) and tests_parameter(node)]
        handled = {name for chain in chains for name in chain_parameters(chain)}
        heard = {key[len("channel."):] for key, entry in TRUTH.addresses().items()
                 if key.startswith("channel.") and "core" in entry["to"]}
        self.assertEqual(sorted(heard - handled), [])


if __name__ == "__main__":
    unittest.main()
