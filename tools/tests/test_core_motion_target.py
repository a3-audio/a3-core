"""Which Motion Core talks to: the rig's own, or the one that arrived last
(spec devices-and-remote-access, decided 2026-10-05).

A Motion on a3nuc2 or the notebook runs instead of the rig's, never beside
it. It says hello like the desk and StemDeck do; Core points its Motion target
at the sender and falls back to the rig's own after a minute of silence. No
configuration to switch: start Motion where you want to play.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_motion import MotionTarget, is_this_machine  # noqa: E402

DEFAULT = ("127.0.0.1", 7771)
OWN = {"192.168.8.10"}
A3NUC2 = "192.168.8.20"
NOTEBOOK = "192.168.43.70"


def target():
    return MotionTarget(DEFAULT, remote_port=7771, own_hosts=OWN,
                        silence_after=60)


class ThisMachine(unittest.TestCase):
    def test_loopback_is_this_machine(self):
        self.assertTrue(is_this_machine("127.0.0.1", OWN))
        self.assertTrue(is_this_machine("127.0.1.1", OWN))

    def test_cores_own_address_is_this_machine(self):
        self.assertTrue(is_this_machine("192.168.8.10", OWN))

    def test_another_machine_is_not(self):
        self.assertFalse(is_this_machine(A3NUC2, OWN))

    def test_a_name_is_compared_as_written(self):
        self.assertFalse(is_this_machine("a3nuc2", OWN))


class TheRigsOwnMotion(unittest.TestCase):
    def test_it_is_the_target_before_anybody_speaks(self):
        t = target()
        self.assertEqual(t.current, DEFAULT)
        self.assertFalse(t.remote)

    def test_its_hello_changes_nothing(self):
        t = target()
        self.assertIsNone(t.hello("127.0.0.1", 0.0))
        self.assertIsNone(t.hello("192.168.8.10", 30.0))
        self.assertEqual(t.current, DEFAULT)

    def test_its_silence_changes_nothing(self):
        t = target()
        t.hello("127.0.0.1", 0.0)
        self.assertIsNone(t.silence(61.0))
        self.assertEqual(t.current, DEFAULT)


class ARemoteMotion(unittest.TestCase):
    def test_its_hello_points_core_at_it(self):
        t = target()
        self.assertEqual(t.hello(A3NUC2, 0.0), (A3NUC2, 7771))
        self.assertEqual(t.current, (A3NUC2, 7771))
        self.assertTrue(t.remote)

    def test_its_next_hello_is_no_switch(self):
        t = target()
        t.hello(A3NUC2, 0.0)
        self.assertIsNone(t.hello(A3NUC2, 30.0))

    def test_the_port_is_the_truths_not_the_default(self):
        t = MotionTarget(("127.0.0.1", 17771), remote_port=7771,
                         own_hosts=OWN, silence_after=60)
        self.assertEqual(t.hello(A3NUC2, 0.0), (A3NUC2, 7771))

    def test_a_minute_of_silence_falls_back_to_the_rigs(self):
        t = target()
        t.hello(A3NUC2, 0.0)
        self.assertIsNone(t.silence(59.0))
        self.assertEqual(t.silence(61.0), DEFAULT)
        self.assertFalse(t.remote)
        self.assertIsNone(t.silence(90.0))

    def test_the_rigs_running_motion_does_not_take_it_back(self):
        """Its hello every 30 s is not an arrival; a remote that arrived
        later keeps the link until it falls silent."""
        t = target()
        t.hello("127.0.0.1", 0.0)
        t.hello(A3NUC2, 10.0)
        self.assertIsNone(t.hello("127.0.0.1", 30.0))
        self.assertTrue(t.remote)

    def test_the_rigs_motion_started_again_takes_over(self):
        t = target()
        t.hello(A3NUC2, 0.0)
        self.assertEqual(t.hello("127.0.0.1", 10.0), DEFAULT)
        self.assertFalse(t.remote)

    def test_the_newest_remote_wins(self):
        t = target()
        t.hello(A3NUC2, 0.0)
        self.assertEqual(t.hello(NOTEBOOK, 10.0), (NOTEBOOK, 7771))

    def test_after_the_fallback_the_remote_can_arrive_again(self):
        t = target()
        t.hello(A3NUC2, 0.0)
        t.silence(61.0)
        self.assertEqual(t.hello(A3NUC2, 70.0), (A3NUC2, 7771))
