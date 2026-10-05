"""Core passes the analyzer's meters on to a remote Motion (spec
devices-and-remote-access).

The beat-analyzer sends /vu to fixed targets from its .env; one of them is
Core's own vu-relay port, and Core forwards each packet there to the Motion
it follows -- unchanged, never parsed, and only while that Motion is not the
rig's own. A port of its own, so 25 bundles a second never queue in front
of the desk's commands on Core's control port.
"""

import ast
import socket
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc   # noqa: E402
import a3_osc_render   # noqa: E402
from a3_core_vu_relay import forward, relay   # noqa: E402

CORE = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()
TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")
BUNDLE = b"#bundle\x00" + bytes(range(40))


def _times(n):
    """running() for n passes of the loop, then stop."""
    return iter([True] * n + [False]).__next__


class EveryPacket(unittest.TestCase):
    def run_with(self, packets, destination):
        sent = []
        incoming = iter(packets)
        forward(lambda: next(incoming), lambda data, to: sent.append((data, to)),
                destination, running=_times(len(packets)))
        return sent

    def test_goes_on_unchanged_to_a_remote_motion(self):
        sent = self.run_with([BUNDLE, b"x"], lambda: ("192.168.8.20", 7772))
        self.assertEqual(sent, [(BUNDLE, ("192.168.8.20", 7772)),
                                (b"x", ("192.168.8.20", 7772))])

    def test_is_dropped_with_the_rigs_own(self):
        self.assertEqual(self.run_with([BUNDLE], lambda: None), [])

    def test_follows_the_target_as_it_moves(self):
        targets = iter([None, ("192.168.8.20", 7772), None])
        sent = self.run_with([b"1", b"2", b"3"], lambda: next(targets))
        self.assertEqual(sent, [(b"2", ("192.168.8.20", 7772))])

    def test_a_failed_send_does_not_stop_the_relay(self):
        """A remote Motion that went away is an ICMP error on the next send;
        the meters of the next one to arrive must still flow."""
        sent = []

        def send(data, to):
            if data == b"1":
                raise OSError("connection refused")
            sent.append(data)

        incoming = iter([b"1", b"2"])
        forward(lambda: next(incoming), send, lambda: ("10.0.0.5", 1),
                running=_times(2))
        self.assertEqual(sent, [b"2"])


class OnARealSocket(unittest.TestCase):
    def test_the_bytes_arrive_as_they_were_sent(self):
        inbox = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        inbox.bind(("127.0.0.1", 0))
        motion = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        motion.bind(("127.0.0.1", 0))
        motion.settimeout(2)
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sender.sendto(BUNDLE, inbox.getsockname())
        relay(inbox, lambda: motion.getsockname(), running=_times(1))
        self.assertEqual(motion.recv(1024), BUNDLE)
        for s in (inbox, motion, sender):
            s.close()


class TheTruth(unittest.TestCase):
    def test_core_has_a_vu_relay_port_of_its_own(self):
        listener = TRUTH.listener("core", "vu-relay")
        self.assertEqual(listener["host"], "local")
        self.assertNotEqual(listener["port"], TRUTH.port("core", "osc"))
        self.assertIn("OSC_VU_core", listener["carries"])

    def test_the_analyzer_sends_its_meters_there(self):
        self.assertIn({"from": "beat-analyzer", "to": "core.vu-relay",
                       "carries": "vu"}, TRUTH.routes())

    def test_core_sends_them_to_motions_vu_port(self):
        self.assertIn({"from": "core", "to": "motion.vu"}, TRUTH.routes())

    def test_the_analyzers_env_gets_the_line(self):
        """Core renders the analyzer's block at its start, so the line reaches
        the rig's build/.env without a hand edit."""
        host, port = TRUTH.endpoint("core", "vu-relay")
        self.assertIn(f"OSC_VU_core={host}:{port}\n",
                      a3_osc_render.analyzer_block(TRUTH))


class CoreRunsIt(unittest.TestCase):
    def test_on_a_thread_of_its_own_at_the_truths_port(self):
        self.assertIn('_truth.endpoint("core", "vu-relay")', CORE)
        self.assertIn("target=relay", CORE)

    def test_towards_the_motion_it_follows(self):
        self.assertIn("_motion.vu_destination()", CORE)

    def test_core_still_parses(self):
        ast.parse(CORE)
