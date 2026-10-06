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
    """Only the cursors and the return's mode are Core's own; the switches are
    StemDeck's (specs stemdeck-remote, desk input selector 2026-10-04)."""

    def test_the_cursors_and_mode_are_written_down(self):
        s = Stems()
        s.cursors[1] = 2
        s.return_mode = 0
        stems = state_of([], _Master(), s)["stems"]
        self.assertEqual(stems["cursors"][1], 2)
        self.assertEqual(stems["return_mode"], 0)

    def test_a_state_without_stems_starts_on_a(self):
        self.assertEqual(apply_stems({}).cursors, [8] * 4)
        self.assertEqual(apply_stems({"stems": "garbage"}).masks, [0] * 8)

    def test_the_cursors_survive_the_round_trip(self):
        s = Stems()
        s.cursors[3] = 4
        self.assertEqual(apply_stems(state_of([], _Master(), s)).cursors[3], 4)


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

    def test_the_hello_and_the_silence_ask_the_watch(self):
        # The sequences themselves are tested in test_core_presence.
        self.assertIn("_stemdeck_watch.hello(", _source_of("stemdeck_said_hello"))
        self.assertIn("_stemdeck_watch.silence(", _source_of("notice_stemdeck_silence"))

    def test_a_stemdeck_hello_is_heard_before_the_silence_check(self):
        source = _source_of("osc_handler_device_hello")
        self.assertLess(source.index("stemdeck_said_hello("),
                        source.index("notice_stemdeck_silence("))

    def test_every_hello_checks_for_silence(self):
        self.assertIn("notice_stemdeck_silence(", _source_of("osc_handler_device_hello"))

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
        calls = _calls_in("say_the_whole_state")
        self.assertEqual(1, len([call for call in calls
                                 if _is_named(call.func, "send_cue_levels")]))
        self.assertIn("say_the_whole_state(", _source_of("osc_handler_recall"))


class TheReturnIsInTheCueLevels(unittest.TestCase):
    def test_send_cue_levels_sets_both_return_sends(self):
        source = ast.get_source_segment(CORE, next(
            node for node in ast.walk(ast.parse(CORE))
            if isinstance(node, ast.FunctionDef) and node.name == "send_cue_levels"))
        self.assertIn('"return_mix"', source)
        self.assertIn('"return_cue"', source)


class TheReturnCueIsWired(unittest.TestCase):
    """Spec return-cue: a push on the CUE field toggles the return's cue,
    which opens its cue send and lights its lamp at once."""

    def test_the_cue_levels_know_the_return_cue(self):
        self.assertIn("master_info.return_cue", _source_of("send_cue_levels"))

    def test_a_push_on_cue_toggles_it(self):
        branch = _branch_of("osc_handler_aux_return", 'elif key == "aux-return.stem.push":')
        self.assertIn("_stems.return_cursor == CUE_FIELD", branch)
        self.assertIn("toggle_return_cue()", branch)

    def test_a_toggle_flips_lights_and_sounds(self):
        source = _source_of("toggle_return_cue")
        self.assertIn("master_info.return_cue = not master_info.return_cue", source)
        self.assertIn("return_cue_lamp(", source)
        self.assertIn("broadcast(", source)
        self.assertIn("send_cue_levels()", source)

    def test_the_return_handler_remembers(self):
        calls = _calls_in("osc_handler_aux_return")
        self.assertEqual(1, len([call for call in calls
                                 if _is_named(call.func, "remember_state")]))


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


class TheTidyIsWired(unittest.TestCase):
    """Spec desk-stem-grid-2: a report pokes the settle, the loop's tick
    tidies -- never the report handler itself."""

    def test_a_report_pokes_and_does_not_tidy(self):
        handler = _source_of("osc_handler_stemdeck")
        self.assertIn("_tidy_settle.poke(", handler)
        self.assertNotIn(".tidy(", handler)

    def test_the_loop_ticks_the_tidy(self):
        self.assertIn("tick=on_tick", CORE)
        self.assertIn("tidy_when_settled()", _source_of("on_tick"))
        self.assertIn("_stems.tidy()", _source_of("tidy_when_settled"))


class TheSelectorIsWired(unittest.TestCase):
    """Spec desk-stem-selector: turn selects, push loads, cue through C."""

    def test_a_turn_only_selects(self):
        branch = _branch_of("osc_handler_channel", 'elif parameter == "stem.turn":')
        self.assertIn("_stems.turn(", branch)
        self.assertNotIn("send_to_stemdeck", branch)

    def test_a_push_loads(self):
        branch = _branch_of("osc_handler_channel", 'elif parameter == "stem.push":')
        self.assertIn("_stems.push(", branch)

    def test_a_push_without_stemdeck_sends_nothing(self):
        branch = _branch_of("osc_handler_channel", 'elif parameter == "stem.push":')
        self.assertIn("_stemdeck_client is not None", branch)

    def test_the_return_switches_its_mode_with_or_without_stemdeck(self):
        branch = _branch_of("osc_handler_aux_return", 'elif key == "aux-return.stem.push":')
        self.assertLess(branch.index("_stems.push(RETURN"),
                        branch.index("_stemdeck_client is not None"))

    def test_a_push_tells_stems_whether_stemdeck_is_there(self):
        branch = _branch_of("osc_handler_channel", 'elif parameter == "stem.push":')
        self.assertIn("connected=_stemdeck_client is not None", branch)
        ret = _branch_of("osc_handler_aux_return", 'elif key == "aux-return.stem.push":')
        self.assertIn("connected=_stemdeck_client is not None", ret)

    def test_a_push_is_said_with_or_without_stemdeck(self):
        """The desk must hear what a push did, and the push must reach the
        selector without StemDeck too (it then moves only the cursor)."""
        branch = _branch_of("osc_handler_channel", 'elif parameter == "stem.push":')
        push = branch.index("_stems.push(")
        guard = branch.index("_stemdeck_client is not None")
        self.assertLess(push, guard)
        self.assertIn("speak_stems()", branch)

    def test_the_cue_sends_the_levels_and_nothing_to_stemdeck(self):
        branch = _branch_of("osc_handler_channel", 'elif parameter == "cue":')
        self.assertIn("send_cue_levels()", branch)
        self.assertNotIn("send_to_stemdeck", branch)

    def test_the_cue_send_does_not_ask_where_stems_are(self):
        self.assertNotIn("channel_mask", _source_of("send_cue_levels"))

    def test_the_old_c_switches_are_cleared_once(self):
        hello = _source_of("stemdeck_said_hello")
        self.assertIn("clear_the_old_cue_switches()", hello)
        clear = _source_of("clear_the_old_cue_switches")
        self.assertIn("if master_info.cue_switches_cleared", clear)
        self.assertIn("send_to_stemdeck(_stems.all_cue_off())", clear)
        self.assertIn("master_info.cue_switches_cleared = True", clear)
        self.assertIn("remember_state()", clear)

    def test_nothing_drives_stemdecks_c_any_more(self):
        self.assertNotIn("apply_stem_cue", CORE)
        self.assertNotIn("cue_commands", CORE)


def _branch_of(function_name, head):
    return _source_of(function_name).split(head, 1)[1].split("\n    elif ", 1)[0]


if __name__ == "__main__":
    unittest.main()
