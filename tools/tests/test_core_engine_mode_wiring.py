# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Core's engine mode, wired into a3-core.py (engine plan 4, task B4).

Read off the source, like test_core_gate_wiring: Core cannot be started in a
test (package paths, sockets). The decisions are in test_core_audio_engine,
test_core_decks and test_core_engine; this holds that a3-core.py asks them, at
the right places -- and that REAPER mode's paths stay exactly as on main.
"""

import ast
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE_PATH = ("platform-config/debian-x86_64/a3-core/home/aaa/.local"
             "/bin/a3-core.py")
CORE = (ROOT / CORE_PATH).read_text()
TREE = ast.parse(CORE)

#: The send points of rule 2: each one has its engine branch.
SEND_POINTS = ("osc_handler_channel", "osc_handler_master",
               "osc_handler_filter", "speak_stems", "toggle_return_cue",
               "osc_handler_beat")


def _function(source, tree, name):
    node = next(n for n in ast.walk(tree)
                if isinstance(n, ast.FunctionDef) and n.name == name)
    return ast.get_source_segment(source, node)


def _source_of(name):
    return _function(CORE, TREE, name)


def _main_block():
    return CORE.split('if __name__ == "__main__":', 1)[1]


def _open_after_recall_call(source, tree):
    for node in ast.walk(tree):
        if (isinstance(node, ast.Call) and any(
                kw.arg == "target" and isinstance(kw.value, ast.Name)
                and kw.value.id == "open_after_recall"
                for kw in node.keywords)):
            return ast.get_source_segment(source, node)
    raise AssertionError("no thread runs open_after_recall")


def _on_main():
    return subprocess.run(["git", "-C", str(ROOT), "show", f"main:{CORE_PATH}"],
                          capture_output=True, text=True, check=True).stdout


class TheRuleIsAskedOnce(unittest.TestCase):
    def test_the_engine_rule_is_evaluated_once_at_start(self):
        self.assertEqual(1, CORE.count("engine_runs("))
        self.assertIn("_engine_mode, _engine_reason = engine_runs(*probe())",
                      _main_block())
        self.assertIn('print("audio engine: engine")', CORE)
        self.assertIn('print(f"audio engine: reaper: {_engine_reason}")', CORE)

    def test_a_fallback_reason_reaches_the_status(self):
        status = _source_of("engine_status")
        self.assertIn("_engine_reason", status)
        self.assertIn("status=engine_status", _main_block())


class EngineModeLeavesReaperAlone(unittest.TestCase):
    def test_engine_mode_silences_reaper_iem_and_dualdelay(self):
        main = _main_block()
        # After the arguments rebind the clients, or they would undo it.
        silenced = main.split("osc_reaper = client", 1)[1]
        branch = silenced.split("if _engine_mode:", 1)[1].split("\n\n", 1)[0]
        self.assertIn('osc_reaper = SilentClient("reaper")', branch)
        self.assertIn('osc_dualdelay = SilentClient("dualdelay")', branch)
        self.assertIn("udp_clients_iem = tuple(SilentClient(", branch)

    def test_a_silent_client_sends_nothing(self):
        cls = next(n for n in ast.walk(TREE)
                   if isinstance(n, ast.ClassDef) and n.name == "SilentClient")
        body = ast.get_source_segment(CORE, cls)
        self.assertIn("def send_message(self, address, value)", body)
        self.assertNotIn("_client", body)
        self.assertNotIn("traffic", body)

    def test_engine_mode_starts_no_reaper_feedback_nor_replay_thread(self):
        main = _main_block()
        for started in ("target=feedback_server.serve_forever",
                        "target=replay_after_every_start"):
            before = main.split(started, 1)[0]
            last_if = before.rfind("if not _engine_mode:")
            self.assertNotEqual(-1, last_if, started)
            # Nothing at the top level between the guard and the start.
            self.assertNotIn("\n    print(", before[last_if:], started)
        saving = main.find("if args.save_project:")
        self.assertIn("if _engine_mode:\n        args.save_project = False",
                      main[:saving])

    def test_the_project_is_an_engine_session_in_engine_mode(self):
        main = _main_block()
        self.assertIn("_project = EngineSession(evening, current=the_live_evening)",
                      main)
        self.assertIn("_project = ReaperProject(evening)", main)

    def test_the_live_evening_is_cores_own(self):
        # Never what a fresh engine reports: it reports nothing but its hello.
        live = _source_of("the_live_evening")
        self.assertIn("evening_state(_relayed)", live)


class EverySendPointHasItsEngineBranch(unittest.TestCase):
    def test_every_send_point_has_its_engine_branch(self):
        for name in SEND_POINTS:
            with self.subTest(name):
                body = _source_of(name)
                self.assertIn("if _engine_mode", body)
                self.assertIn("send_to_engine(", body)

    def test_channel_values_go_after_the_relay_through_the_deck_filter(self):
        channel = _source_of("osc_handler_channel")
        relayed = channel.find("relay(address, raw, origin)")
        sent = channel.find("send_to_engine(engine_channel_messages(")
        self.assertNotEqual(-1, sent)
        self.assertLess(relayed, sent)

    def test_flags_and_positions_reach_the_engine(self):
        channel = _source_of("osc_handler_channel")
        self.assertIn('flag_message(_truth, "cue", channel_index, wanted)', channel)
        self.assertIn('flag_message(_truth, "filter", channel_index, wanted)', channel)
        self.assertIn('position_message(_truth, "azimuth", channel_index, az)', channel)
        self.assertIn('position_message(_truth, "elevation", channel_index, el)', channel)

    def test_the_other_words(self):
        self.assertIn("forward_messages(", _source_of("osc_handler_master"))
        flt = _source_of("osc_handler_filter")
        self.assertIn("forward_messages(", flt)
        self.assertIn("filter_mode_message(", flt)
        self.assertIn("source_messages(", _source_of("speak_stems"))
        self.assertIn("return_cue_message(", _source_of("toggle_return_cue"))
        beat = _source_of("osc_handler_beat")
        self.assertIn("tempo_message(_truth, wanted)", beat)
        self.assertLess(beat.find("is NO_TEMPO"), beat.find("tempo_message("))

    def test_nothing_reaches_the_engine_before_its_hello(self):
        sender = _source_of("send_to_engine")
        self.assertIn("if not engine_heard():", sender)
        heard = _source_of("engine_heard")
        self.assertIn("_engine_mode", heard)
        self.assertIn("_project.round", heard)

    def test_the_engine_client_is_the_truths(self):
        self.assertIn('WatchedClient(\n    SimpleUDPClient(*_truth.endpoint("engine", "osc")), "engine")',
                      CORE)


class TheHelloReplays(unittest.TestCase):
    def test_engine_hello_is_mapped(self):
        self.assertIn('dispatcher.map(_truth.address("engine.hello"), '
                      "osc_handler_engine_hello", _main_block())

    def test_the_hello_replays_then_recalls_with_the_token(self):
        hello = _source_of("osc_handler_engine_hello")
        asked = hello.find("_project.heard_hello(")
        replay = hello.find("replay_evening(the_evening_to_replay(), send_to_self)")
        rearm = hello.find("_startup_recall.rearm()")
        token = hello.find("send_to_self(OSC_ADDRESS_RECALL, _startup_recall.token)")
        written = hello.find("if _project.replayed(round_started):")
        self.assertTrue(-1 < asked < replay < rearm < token < written, hello)
        self.assertIn("threading.Thread(", hello)
        self.assertIn("write_the_evening()", hello[written:])

    def test_the_hello_gate_is_not_asked(self):
        # As device.hello: a hello is not a value.
        self.assertNotIn("values_refused(", _source_of("osc_handler_engine_hello"))

    def test_the_engine_recall_sends_state_then_open_and_no_reaper_gate(self):
        recall = _source_of("osc_handler_recall")
        branch = recall.split("if _engine_mode:", 1)[1]
        branch = branch.split("return", 1)[0]
        self.assertIn("_startup_recall.is_it(osc_arguments)", branch)
        state = branch.find("send_to_engine(state_messages(")
        opened = branch.find("send_to_engine([open_message(_truth)])")
        self.assertTrue(-1 < state < opened, branch)
        self.assertNotIn("open_after_recall", branch)
        self.assertNotIn("osc_reaper", branch)


class ReaperModeIsAsOnMain(unittest.TestCase):
    def test_reaper_mode_paths_are_unchanged(self):
        before = _on_main()
        old_tree = ast.parse(before)
        for name in ("replay_once_reaper_is_quiet", "send_cue_levels"):
            with self.subTest(name):
                self.assertEqual(_function(before, old_tree, name),
                                 _source_of(name))
        self.assertEqual(_open_after_recall_call(before, old_tree),
                         _open_after_recall_call(CORE, TREE))


class Decks(unittest.TestCase):
    def test_engine_mode_selector_is_the_deck_selector(self):
        channel = _source_of("osc_handler_channel")
        taken = channel.find(
            'if _engine_mode and take_deck_word(parameter, fields["ch"], value):')
        self.assertNotEqual(-1, taken)
        self.assertLess(taken, channel.find('if parameter == "aux-send":'))
        self.assertIn("return", channel[taken:].split("\n\n", 1)[0])
        words = _source_of("take_deck_word")
        self.assertIn('if parameter in ("stem.turn", "stem.push"):', words)
        self.assertIn("select_deck(parameter, channel, value)", words)
        # REAPER mode still asks a3_core_stems.
        self.assertIn("_stems.turn(channel_index, int(value))", channel)
        self.assertIn("_stems.push(channel_index", channel)

        select = _source_of("select_deck")
        self.assertIn("_decks.turn(channel, int(value))", select)
        pushed = select.find("changes = _decks.push(channel)")
        left = select.find("_decks.leave_messages(_truth, changes)")
        self.assertTrue(-1 < pushed < left, select)
        # Straight after the push: it reads what that push saved.
        self.assertNotIn("_decks.", select[pushed + 1:left].split("\n", 1)[1]
                         .rsplit("\n", 1)[0])
        self.assertIn("speak_stems()", select)
        self.assertIn("send_to_deck(", select)

    def test_the_decks_exist_in_engine_mode_only(self):
        main = _main_block()
        guarded = [node for node in ast.walk(TREE)
                   if isinstance(node, ast.If) and isinstance(node.test, ast.Name)
                   and node.test.id == "_engine_mode"]
        built = [stmt for node in guarded for stmt in node.body
                 if isinstance(stmt, ast.Assign)
                 and any(isinstance(t, ast.Name) and t.id == "_decks"
                         for t in stmt.targets)]
        self.assertEqual(1, len(built))
        self.assertIn("DeckSelector(", ast.get_source_segment(CORE, built[0]))
        self.assertEqual(1, CORE.count("_decks = DeckSelector("))
        self.assertIn("_decks.from_evening(_evening_file.load())", main)
        self.assertIn("_decks.to_evening()", _source_of("write_the_evening"))

    def test_sources_and_presence_are_reported(self):
        report = _source_of("report_decks")
        self.assertIn('"channel.source"', report)
        self.assertIn('"stemdeck.present"', report)
        self.assertIn("report_decks(full)", _source_of("speak_stems"))
        self.assertIn("notice_deck_presence(", _source_of("on_tick"))

    def test_three_d_on_a_deck_channel_goes_to_the_deck(self):
        words = _source_of("take_deck_word")
        branch = words.split(
            'if parameter == "3d" and _decks.deck_channel(channel):', 1)
        self.assertEqual(2, len(branch), "no deck branch for 3d")
        body = branch[1].split("return False", 1)[0]
        self.assertIn("send_to_deck(_decks.three_d_messages(_truth, channel, value))",
                      body)
        self.assertIn("return True", body)
        self.assertNotIn("send_to_engine", body)
        self.assertNotIn("apply_3d_crossfade", body)
        # Taken before the chain, whose 3d branch would otherwise crossfade.
        channel = _source_of("osc_handler_channel")
        self.assertLess(channel.find("take_deck_word("),
                        channel.find('elif parameter == "3d":'))

    def test_deck_hello_is_mapped_and_resends_three_d(self):
        main = _main_block()
        self.assertIn('_truth.address("stemdeck.deck-hello", deck=deck)', main)
        self.assertIn("osc_handler_deck_hello", main)
        hello = _source_of("osc_handler_deck_hello")
        heard = hello.find("_decks.heard(deck, now)")
        resend = hello.find("send_to_deck(_decks.hello_messages(")
        self.assertTrue(-1 < heard < resend, hello)
        # The family handler leaves it to its own.
        self.assertIn('"stemdeck.deck-hello"', _source_of("osc_handler_stemdeck"))

    def test_deck_words_go_to_the_decks_own_host(self):
        sender = _source_of("send_to_deck")
        self.assertIn("_deck_clients.get(", sender)


if __name__ == "__main__":
    unittest.main()
