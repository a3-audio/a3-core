import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]
                       / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_hello_gate import ADMIT, REFUSE, REFUSE_DIFFERS_FIRST, REFUSE_FIRST, HelloGate  # noqa: E402


class ValuesOnlyAfterHello(unittest.TestCase):
    def test_a_host_that_has_not_said_hello_is_refused(self):
        self.assertEqual(HelloGate().check("192.168.8.20"), REFUSE_FIRST)

    def test_the_refusal_is_news_only_once_per_host(self):
        gate = HelloGate()
        gate.check("192.168.8.20")
        self.assertEqual(gate.check("192.168.8.20"), REFUSE)
        self.assertEqual(gate.check("192.168.8.11"), REFUSE_FIRST)

    def test_a_host_that_said_hello_is_admitted(self):
        gate = HelloGate()
        gate.greet("192.168.8.20", True)
        self.assertEqual(gate.check("192.168.8.20"), ADMIT)

    def test_a_hello_opens_only_its_own_host(self):
        gate = HelloGate()
        gate.greet("192.168.8.11", True)
        self.assertEqual(gate.check("192.168.8.20"), REFUSE_FIRST)

    def test_a_new_run_starts_closed(self):
        gate = HelloGate()
        gate.greet("192.168.8.20", True)
        self.assertEqual(HelloGate().check("192.168.8.20"), REFUSE_FIRST)

    def test_after_a_hello_a_later_refusal_is_news_again_never(self):
        gate = HelloGate()
        gate.check("a")
        gate.greet("a", True)
        self.assertEqual(gate.check("a"), ADMIT)

    def test_a_trusted_source_is_admitted_without_opening_its_host(self):
        gate = HelloGate()
        gate.trust(("127.0.0.1", 40000))
        self.assertEqual(gate.check("127.0.0.1", 40000), ADMIT)
        self.assertEqual(gate.check("127.0.0.1", 40001), REFUSE_FIRST)


class OnlyOnCoresOwnTruth(unittest.TestCase):
    def test_a_hello_with_another_truth_does_not_open(self):
        gate = HelloGate()
        gate.greet("h", False)
        self.assertEqual(gate.check("h"), REFUSE_DIFFERS_FIRST)
        self.assertEqual(gate.check("h"), REFUSE)

    def test_a_later_matching_hello_opens(self):
        gate = HelloGate()
        gate.greet("h", False)
        gate.greet("h", True)
        self.assertEqual(gate.check("h"), ADMIT)

    def test_a_later_differing_hello_closes_again_and_is_said_again(self):
        gate = HelloGate()
        gate.greet("h", True)
        gate.greet("h", False)
        self.assertEqual(gate.check("h"), REFUSE_DIFFERS_FIRST)
