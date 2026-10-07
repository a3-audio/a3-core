"""Core opens the gate after its own start-up recall (a3-system#74).

Read off the source, like test_core_return_wiring: Core cannot be started in
a test. The decisions are in test_core_gate; this holds that a3-core.py asks
them, at the right place.
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


class TheFeedbackFeedsTheGate(unittest.TestCase):
    def test_the_gate_is_built_from_the_layout(self):
        self.assertIn("_gate = Gate(_layout.gate)", CORE)

    def test_gate_reports_are_taken_after_the_echo_and_before_the_layout(self):
        handler = _source_of("reaper_feedback_handler")
        taken = handler.find("_gate.heard(track")
        self.assertNotEqual(-1, taken)
        self.assertGreater(taken, handler.find("echo_filter.is_stale("))
        self.assertLess(taken, handler.find("_layout.track_role("))

    def test_a_gate_report_is_never_relayed(self):
        handler = _source_of("reaper_feedback_handler")
        branch = handler.split("_gate.heard(track", 1)[1].split("role = ", 1)[0]
        self.assertIn("return", branch)
        self.assertNotIn("broadcast(", branch)
        self.assertNotIn("note_passed_on(", branch)


class TheRecallOpensIt(unittest.TestCase):
    def test_only_cores_own_recall_opens_the_gate(self):
        recall = _source_of("osc_handler_recall")
        self.assertIn("_startup_recall.is_it(osc_arguments)", recall)
        self.assertIn("target=open_after_recall", recall)
        self.assertLess(recall.find("say_the_whole_state()"),
                        recall.find("_startup_recall.is_it("))

    def test_the_gate_talks_to_reaper_only_in_a_thread_of_its_own(self):
        opening = _source_of("osc_handler_recall").split("_startup_recall.is_it(", 1)[1]
        self.assertIn("threading.Thread(", opening)
        self.assertIn("osc_reaper.send_message", opening)
        self.assertNotIn("broadcast", opening)

    def test_the_replay_sends_the_token(self):
        self.assertIn("send_to_self(OSC_ADDRESS_RECALL, _startup_recall.token)", CORE)
        self.assertNotIn("send_to_self(OSC_ADDRESS_RECALL, 1)", CORE)

    def test_the_token_follows_the_wait_unconditionally(self):
        # The 60 s give-up in wait_until_quiet still replays and so still opens.
        path = CORE.split("def replay_once_reaper_is_quiet", 1)[1]
        path = path.split("threading.Thread(", 1)[0]
        waited = path.find("wait_until_quiet(")
        token = path.find("send_to_self(OSC_ADDRESS_RECALL, _startup_recall.token)")
        self.assertLess(waited, token)
        self.assertNotIn("if ", path[waited:token])


if __name__ == "__main__":
    unittest.main()
