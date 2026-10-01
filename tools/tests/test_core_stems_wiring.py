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
    """Only the return cursor is Core's own; the switches are StemDeck's
    (spec stemdeck-remote)."""

    def test_the_cursor_is_written_down(self):
        s = Stems()
        s.return_cursor = 5
        self.assertEqual(state_of([], _Master(), s)["stems"], {"return_cursor": 5})

    def test_a_state_without_stems_is_a_fresh_mirror(self):
        self.assertEqual(apply_stems({}).masks, [0] * 8)
        self.assertEqual(apply_stems({"stems": "garbage"}).masks, [0] * 8)

    def test_the_cursor_survives_the_round_trip(self):
        s = Stems()
        s.return_cursor = 3
        self.assertEqual(apply_stems(state_of([], _Master(), s)).return_cursor, 3)


class _Master:
    class _Mode:
        value = "low_pass"
    fx_mode = _Mode()


class CueGoesThroughTheSends(unittest.TestCase):
    """Since 2026-10-01 the cue is the channel buses' sends to enc_phones."""

    def test_the_channel_cue_has_its_branch(self):
        self.assertIn('elif parameter == "cue":', CORE)
        self.assertNotIn('parameter == "pfl"', CORE)

    def test_the_stems_cue_send_is_not_gated_by_core(self):
        source = _source_of("send_cue_levels")
        self.assertNotIn("any_cued", source)
        self.assertNotIn("stem_cue", source)


class StemDeckIsWired(unittest.TestCase):
    def test_the_stemdeck_family_is_mapped(self):
        self.assertIn('("stemdeck", osc_handler_stemdeck)', CORE)

    def test_the_stem_cue_toggle_is_gone(self):
        self.assertNotIn("def toggle_stem_cue", CORE)
        self.assertNotIn("osc_handler_stem)", CORE)
        self.assertNotIn("stem.cue", CORE)

    def test_a_returning_stemdeck_is_asked_for_everything(self):
        self.assertIn("stemdeck_said_hello(", _source_of("osc_handler_device_hello"))
        self.assertIn('"stemdeck.recall"', _source_of("stemdeck_said_hello"))

    def test_silence_unmutes_analog_and_shows_a(self):
        calls = _calls_in("notice_stemdeck_silence")
        names = {c.func.attr if isinstance(c.func, ast.Attribute) else getattr(c.func, "id", "")
                 for c in calls}
        self.assertTrue({"forget", "speak_stems"} <= names, names)

    def test_every_hello_checks_for_silence(self):
        self.assertIn("notice_stemdeck_silence(", _source_of("osc_handler_device_hello"))

    def test_a_channel_turn_waits_for_stemdeck(self):
        source = _source_of("osc_handler_channel")
        branch = source.split('elif parameter == "stem.turn":', 1)[1].split("elif ", 1)[0]
        self.assertIn("send_to_stemdeck", branch)
        self.assertNotIn("speak_stems", branch)

    def test_a_report_is_announced(self):
        self.assertIn("speak_stems(", _source_of("osc_handler_stemdeck"))

    def test_the_analog_sends_go_to_reaper(self):
        self.assertIn("analog_messages(", _source_of("speak_stems"))


def _source_of(function_name):
    node = next(n for n in ast.walk(ast.parse(CORE))
                if isinstance(n, ast.FunctionDef) and n.name == function_name)
    return ast.get_source_segment(CORE, node)


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


class TheReturnIsInTheCueLevels(unittest.TestCase):
    def test_send_cue_levels_sets_both_return_sends(self):
        source = ast.get_source_segment(CORE, next(
            node for node in ast.walk(ast.parse(CORE))
            if isinstance(node, ast.FunctionDef) and node.name == "send_cue_levels"))
        self.assertIn('"return_mix"', source)
        self.assertIn('"return_cue"', source)


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
