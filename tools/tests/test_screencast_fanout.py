"""The screencast serves several viewers from one encoder.

A second viewer (the pro-DJ app beside OBS) must cost no second encode, and a
viewer that falls behind (Wi-Fi) is dropped, never waited for.
"""

import socket
import subprocess
import sys
import threading
import time
import unittest
from pathlib import Path

HOME = Path(__file__).resolve().parents[2] / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(HOME / "lib"))
import a3_screencast_fanout as fanout  # noqa: E402

FAKE_ENCODER = ("import sys, time\n"
                "while True:\n"
                "    sys.stdout.buffer.write(b'TS'); sys.stdout.buffer.flush(); time.sleep(0.01)\n")


class TheFanout(unittest.TestCase):
    def test_every_viewer_gets_every_chunk(self):
        f = fanout.Fanout()
        a, b = object(), object()
        f.add(a)
        f.add(b)
        f.feed(b"ab")
        self.assertEqual(b"ab", f.pending(a))
        self.assertEqual(b"ab", f.pending(b))

    def test_sent_bytes_leave_the_queue(self):
        f = fanout.Fanout()
        a = object()
        f.add(a)
        f.feed(b"abcd")
        f.sent(a, 3)
        self.assertEqual(b"d", f.pending(a))

    def test_a_slow_viewer_is_dropped_not_waited_for(self):
        f = fanout.Fanout(limit_bytes=10)
        slow, quick = object(), object()
        f.add(slow)
        f.add(quick)
        f.feed(b"x" * 5)
        f.sent(quick, 5)
        dropped = f.feed(b"x" * 6)
        self.assertEqual([slow], dropped)
        self.assertEqual([quick], f.viewers())

    def test_a_ninth_viewer_is_refused(self):
        f = fanout.Fanout(max_viewers=8)
        results = [f.add(object()) for _ in range(9)]
        self.assertEqual([True] * 8 + [False], results)


class TheGate(unittest.TestCase):
    def test_the_encoder_starts_with_the_first_viewer_and_lingers(self):
        gate = fanout.EncoderGate(linger=10)
        self.assertFalse(gate.wanted(0, 0))
        self.assertTrue(gate.wanted(1, 1))
        self.assertTrue(gate.wanted(0, 5))
        self.assertFalse(gate.wanted(0, 11.1))


class Serving(unittest.TestCase):
    def setUp(self):
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.started = []
        self.clients = []

    def tearDown(self):
        self.stop.set()
        self.thread.join(5)
        for c in self.clients:
            c.close()
        for p in self.started:
            if p.poll() is None:
                p.kill()
            p.wait()
        self.listener.close()

    def start_encoder(self):
        p = subprocess.Popen([sys.executable, "-c", FAKE_ENCODER], stdout=subprocess.PIPE)
        self.started.append(p)
        return p

    def run_serve(self, linger):
        self.thread = threading.Thread(
            target=fanout.serve, args=(self.listener, self.start_encoder),
            kwargs={"stop": self.stop, "linger": linger}, daemon=True)
        self.thread.start()

    def connect(self):
        c = socket.create_connection(("127.0.0.1", self.port), timeout=5)
        self.clients.append(c)
        return c

    def test_a_second_viewer_costs_no_second_encoder(self):
        self.run_serve(linger=10)
        a, b = self.connect(), self.connect()
        self.assertEqual(b"TS", a.recv(2))
        self.assertEqual(b"TS", b.recv(2))
        self.assertEqual(1, len(self.started))

    def test_the_encoder_stops_when_the_last_viewer_has_gone(self):
        self.run_serve(linger=0.2)
        a, b = self.connect(), self.connect()
        a.recv(2)
        b.recv(2)
        a.close()
        b.close()
        time.sleep(0.5)
        self.assertEqual(1, len(self.started))
        self.assertIsNotNone(self.started[0].poll())


if __name__ == "__main__":
    unittest.main()
