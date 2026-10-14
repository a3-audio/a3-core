"""Core keeps the evening when REAPER closes and comes back under it.

Read off the source, like test_core_gate_wiring: Core cannot be started in a
test. The decisions are in test_core_reaper_restart; this holds that
a3-core.py asks them, at the right places.
"""

import ast
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
        / "bin/a3-core.py").read_text()


def _source_of(function_name):
    node = next(n for n in ast.walk(ast.parse(CORE))
                if isinstance(n, ast.FunctionDef) and n.name == function_name)
    return ast.get_source_segment(CORE, node)


class TheFeedbackWatchesTheProject(unittest.TestCase):
    def setUp(self):
        self.handler = _source_of("reaper_feedback_handler")

    def test_every_report_is_shown_to_the_project_before_any_filter(self):
        watched = self.handler.find("reaper_project_closed(address, value)")
        self.assertNotEqual(-1, watched)
        self.assertLess(self.handler.find("reaper_arrivals.tick()"), watched)
        self.assertLess(watched, self.handler.find("echo_filter.is_echo("))
        self.assertLess(watched, self.handler.find("_gate.heard(track"))

    def test_a_closed_project_is_neither_relayed_nor_written(self):
        branch = self.handler.split("reaper_project_closed(address, value)", 1)[1]
        branch = branch.split("echo_filter.is_echo(", 1)[0]
        self.assertIn("return", branch)
        self.assertNotIn("broadcast(", branch)
        self.assertNotIn("note_passed_on(", branch)

    def test_only_a_track_the_layout_names_is_asked_about(self):
        watch = _source_of("reaper_project_closed")
        self.assertIn("_layout.names(", watch)
        self.assertLess(watch.find("_layout.names("),
                        watch.find("_project.report("))


class ACloseStartsTheWaitAgain(unittest.TestCase):
    def setUp(self):
        self.closed = _source_of("reaper_closed_its_project")

    def test_reaper_has_to_answer_again_before_the_next_replay(self):
        cleared = self.closed.find("reaper_heard.clear()")
        gone = self.closed.find("reaper_gone.set()")
        self.assertNotEqual(-1, cleared)
        self.assertLess(cleared, gone)

    def test_the_gate_forgets_the_closed_project(self):
        self.assertIn("_gate.forget()", self.closed)

    def test_echoes_expected_from_the_closed_project_are_dropped(self):
        self.assertIn("echo_filter.forget()", self.closed)

    def test_the_watch_calls_it_on_a_close(self):
        watch = _source_of("reaper_project_closed")
        self.assertIn("== CLOSED", watch)
        self.assertIn("reaper_closed_its_project()", watch)


class TheEveningIsWrittenOnlyWhileLive(unittest.TestCase):
    def test_a_value_passed_on_is_written_only_while_reaper_is_the_evening(self):
        noted = _source_of("note_passed_on")
        self.assertIn("if _project.live:", noted)
        self.assertLess(noted.find("_relayed.note("), noted.find("if _project.live:"))

    def test_the_project_holds_the_evening_read_before_the_port_opens(self):
        held = CORE.find("_project = ReaperProject(evening)")
        self.assertNotEqual(-1, held)
        self.assertLess(CORE.find("evening = list(replayable("), held)
        self.assertLess(held, CORE.find("feedback_server = osc_server.BlockingOSCUDPServer("))


class TheReplayRunsForEveryStart(unittest.TestCase):
    def setUp(self):
        self.replay = _source_of("replay_once_reaper_is_quiet")

    def test_the_round_is_taken_before_the_wait(self):
        self.assertLess(self.replay.find("_project.round"),
                        self.replay.find("wait_until_quiet("))

    def test_it_replays_what_the_project_holds(self):
        """Through the_evening_to_replay, which lays the desk over it
        (test_core_desk_first_wiring)."""
        self.assertIn("the_evening_to_replay()", self.replay)
        self.assertIn("_project.evening()", _source_of("the_evening_to_replay"))

    def test_each_replay_carries_a_fresh_token(self):
        self.assertLess(self.replay.find("_startup_recall.rearm()"),
                        self.replay.find("_startup_recall.token)"))

    def test_after_the_recall_the_evening_is_live_and_written(self):
        token = self.replay.find("_startup_recall.token)")
        replayed = self.replay.find("_project.replayed(")
        self.assertLess(token, replayed)
        self.assertLess(replayed, self.replay.find("write_the_evening()"))

    def test_the_loop_runs_in_a_thread_of_its_own(self):
        start = CORE.find("server = osc_server.BlockingOSCUDPServer(")
        tail = CORE[start:]
        self.assertIn("target=replay_after_every_start", tail)
        loop = tail.split("target=replay_after_every_start", 1)[1]
        loop = loop.split(")).start()", 1)[0]
        self.assertIn("reaper_heard", loop)
        self.assertIn("reaper_gone", loop)
        self.assertLess(tail.find("target=replay_after_every_start"),
                        tail.rfind("serve(server.socket,"))


if __name__ == "__main__":
    unittest.main()
