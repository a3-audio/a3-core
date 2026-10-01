"""Core carries stems on the desk: words mapped, state kept, recall says it."""

import ast
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

from a3_core_stems import Stems                      # noqa: E402
from a3_core_state import apply_stems, state_of      # noqa: E402

CORE = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()


class TheStateFile(unittest.TestCase):
    def test_stems_are_written_down(self):
        s = Stems()
        s.turn_channel(0, +1)
        self.assertEqual(state_of([], _Master(), s)["stems"], s.as_data())

    def test_a_state_without_stems_is_all_none(self):
        self.assertEqual(apply_stems({}).channel_pair, [0, 0, 0, 0])
        self.assertEqual(apply_stems({"stems": "garbage"}).channel_pair, [0, 0, 0, 0])

    def test_stems_survive_the_round_trip(self):
        s = Stems()
        s.turn_channel(2, +3)
        again = apply_stems(state_of([], _Master(), s))
        self.assertEqual(again.channel_pair, s.channel_pair)


class _Master:
    class _Mode:
        value = "low_pass"
    fx_mode = _Mode()


class CueGoesThroughTheSends(unittest.TestCase):
    """Since 2026-10-01 the cue is the channel buses' sends to enc_phones."""

    def test_the_stem_family_is_mapped(self):
        self.assertIn('("stem", osc_handler_stem)', CORE)

    def test_the_channel_cue_has_its_branch(self):
        self.assertIn('elif parameter == "cue":', CORE)
        self.assertNotIn('parameter == "pfl"', CORE)

    def test_cue_knob_and_stem_cue_send_the_levels(self):
        self.assertGreaterEqual(CORE.count("send_cue_levels("), 4)   # def, cue, stem, knob


class CoreListens(unittest.TestCase):
    def test_core_still_parses(self):
        ast.parse(CORE)

    def test_the_return_family_is_mapped(self):
        self.assertIn('("aux-return", osc_handler_aux_return)', CORE)

    def test_the_channel_turn_has_its_branch(self):
        self.assertIn('elif parameter == "stem.turn":', CORE)

    def test_recall_and_start_speak_the_stems(self):
        self.assertEqual(CORE.count("speak_stems(full=True)"), 2)   # recall, start-up


def _calls_in(function_name):
    tree = ast.parse(CORE)
    function = next(node for node in ast.walk(tree)
                    if isinstance(node, ast.FunctionDef)
                    and node.name == function_name)
    return [node for node in ast.walk(function) if isinstance(node, ast.Call)]


def _is_named(node, name):
    return isinstance(node, ast.Name) and node.id == name


class ReaperHearsTheStemsOnceItListens(unittest.TestCase):
    """At start-up the stems go to REAPER before it listens, and are then
    marked as told -- after a cold boot only changes followed, and the desk
    showed pairs REAPER did not play. The replay thread asks Core for a
    recall through its own port once REAPER is quiet, so the full set is
    sent again from the server thread, the only one that touches `_stems`."""

    def setUp(self):
        self.calls = _calls_in("replay_once_reaper_is_quiet")

    def test_the_replay_asks_for_a_recall(self):
        recalls = [call for call in self.calls
                   if any(_is_named(arg, "OSC_ADDRESS_RECALL") for arg in call.args)]
        self.assertEqual(1, len(recalls))

    def test_the_recall_comes_after_the_evening(self):
        evening = next(call for call in self.calls
                       if _is_named(call.func, "replay_evening"))
        recall = next((call for call in self.calls
                       if any(_is_named(arg, "OSC_ADDRESS_RECALL")
                              for arg in call.args)), None)
        self.assertIsNotNone(recall, "no recall in the replay")
        self.assertGreater(recall.lineno, evening.lineno)


class ReaperHearsTheCueOnceItListens(unittest.TestCase):
    """The cue levels go out at start-up before REAPER listens, like the
    stems; the same recall that repeats the stems must repeat them, or after
    a cold boot the phones hear the template's sends, everything at 0 dB."""

    def test_the_recall_sends_the_cue_levels(self):
        calls = _calls_in("osc_handler_recall")
        self.assertEqual(1, len([call for call in calls
                                 if _is_named(call.func, "send_cue_levels")]))


class ThePhonesMixIsWrittenDown(unittest.TestCase):
    """phones_mix is one of the state file's fields; a knob that moves it
    and never writes it comes back from a restart where it last was saved."""

    def test_the_master_handler_remembers(self):
        calls = _calls_in("osc_handler_master")
        self.assertEqual(1, len([call for call in calls
                                 if _is_named(call.func, "remember_state")]))


class TheReturnSaysWhatItCannotServe(unittest.TestCase):
    """A key of the return family that no branch serves is written down as
    unknown, like the channel handler's `else` -- not left looking served."""

    def test_the_last_else_notes_it_as_unknown(self):
        calls = _calls_in("osc_handler_aux_return")
        unknown = [call for call in calls
                   if isinstance(call.func, ast.Attribute)
                   and _is_named(call.func.value, "traffic")
                   and call.func.attr == "unknown"]
        self.assertEqual(1, len(unknown))


if __name__ == "__main__":
    unittest.main()
