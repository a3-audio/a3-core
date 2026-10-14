"""A send that fails is not the end of the message (2026-10-14).

Right after a boot the network came up some two minutes after Core. A REAPER
report Core passed on to the desk then raised "Network is unreachable" inside
the feedback handler: the exception ended the handler, so the subscribers
after the desk and the rest of that report never got it. Every outgoing
client is a WatchedClient, so the send there catches the error, says it once
per destination until a send gets through again, and carries on.
"""

import errno
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

from a3_core_send_failures import SendFailures   # noqa: E402

CORE = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()


def unreachable():
    raise OSError(errno.ENETUNREACH, "Network is unreachable")


class Recorder:
    def __init__(self):
        self.said = []

    def __call__(self, line):
        self.said.append(line)


class AFailedSend(unittest.TestCase):
    def setUp(self):
        self.say = Recorder()
        self.failures = SendFailures(self.say)

    def test_it_does_not_raise(self):
        self.assertFalse(self.failures.send("mixer", unreachable))

    def test_it_is_said_once(self):
        for _ in range(5):
            self.failures.send("mixer", unreachable)
        self.assertEqual(1, len(self.say.said))
        self.assertIn("mixer", self.say.said[0])
        self.assertIn("Network is unreachable", self.say.said[0])

    def test_each_destination_is_said_on_its_own(self):
        self.failures.send("mixer", unreachable)
        self.failures.send("motion", unreachable)
        self.assertEqual(2, len(self.say.said))

    def test_a_send_that_gets_through_is_said_and_rearms(self):
        self.failures.send("mixer", unreachable)
        self.assertTrue(self.failures.send("mixer", lambda: None))
        self.failures.send("mixer", unreachable)
        self.assertEqual(3, len(self.say.said))
        self.assertIn("again", self.say.said[1])

    def test_a_good_send_says_nothing(self):
        self.assertTrue(self.failures.send("mixer", lambda: None))
        self.assertEqual([], self.say.said)

    def test_only_os_errors_are_caught(self):
        def broken():
            raise ValueError("a bug, not the network")
        with self.assertRaises(ValueError):
            self.failures.send("mixer", broken)


class EveryOutgoingClientUsesIt(unittest.TestCase):
    def test_the_watched_client_sends_through_it(self):
        body = CORE[CORE.index("class WatchedClient"):]
        body = body[:body.index("\nosc_a3mixer")]
        self.assertIn("_send_failures.send(self.name", body)
        self.assertNotRegex(body, r"\n        self\._client\.send_message\(address, value\)\n")


if __name__ == "__main__":
    unittest.main()
