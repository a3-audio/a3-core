"""The screencast serves several viewers from one encoder.

A second viewer (the pro-DJ app beside OBS) must cost no second encode, and a
viewer that falls behind (Wi-Fi) is dropped, never waited for.
"""

import signal
import socket
import subprocess
import sys
import threading
import time
import unittest
from unittest import mock
from pathlib import Path

HOME = Path(__file__).resolve().parents[2] / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
sys.path.insert(0, str(HOME / "lib"))
import a3_screencast_fanout as fanout  # noqa: E402

FAKE_ENCODER = ("import sys, time\n"
                "while True:\n"
                "    sys.stdout.buffer.write(b'TS'); sys.stdout.buffer.flush(); time.sleep(0.01)\n")
SHORT_ENCODER = ("import sys, time\n"
                 "sys.stdout.buffer.write(b'TS'); sys.stdout.buffer.flush(); time.sleep(0.1)\n")
DEAF_ENCODER = ("import signal, sys, time\n"
                "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
                "while True:\n"
                "    sys.stdout.buffer.write(b'TS'); sys.stdout.buffer.flush(); time.sleep(0.01)\n")
FIREHOSE_ENCODER = ("import sys, time\n"
                    "while True:\n"
                    "    sys.stdout.buffer.write(b'x' * 65536); sys.stdout.buffer.flush(); time.sleep(0.001)\n")
DEADLINE = 5.0


def wait_until(condition, timeout=DEADLINE):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if condition():
            return True
        time.sleep(0.02)
    return condition()


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
        self.thread = None
        self.script = FAKE_ENCODER
        for name, value in (("RESTART_AFTER", 0.1), ("KILL_AFTER", 0.2)):
            patcher = mock.patch.object(fanout, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def tearDown(self):
        self.stop.set()
        if self.thread:
            self.thread.join(DEADLINE)
        for c in self.clients:
            c.close()
        for p in self.started:
            if p.poll() is None:
                p.kill()
            p.wait()
        self.listener.close()

    def start_encoder(self):
        p = subprocess.Popen([sys.executable, "-c", self.script], stdout=subprocess.PIPE)
        self.started.append(p)
        return p

    def run_serve(self, linger=10, **kwargs):
        self.thread = threading.Thread(
            target=fanout.serve, args=(self.listener, self.start_encoder),
            kwargs={"stop": self.stop, "linger": linger, **kwargs}, daemon=True)
        self.thread.start()

    def connect(self, rcvbuf=None):
        c = socket.socket()
        if rcvbuf:
            c.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
        c.settimeout(DEADLINE)
        c.connect(("127.0.0.1", self.port))
        self.clients.append(c)
        return c

    def test_a_second_viewer_costs_no_second_encoder(self):
        self.run_serve()
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
        self.assertTrue(wait_until(lambda: self.started[0].poll() is not None))
        self.assertEqual(1, len(self.started))

    def test_an_encoder_that_exits_is_started_again_while_a_viewer_waits(self):
        self.script = SHORT_ENCODER
        self.run_serve()
        viewer = self.connect()
        self.assertEqual(b"TS", viewer.recv(2))
        self.assertTrue(wait_until(lambda: len(self.started) >= 2))
        self.assertEqual(b"TS", viewer.recv(2))

    def test_an_encoder_that_ignores_terminate_is_killed(self):
        self.script = DEAF_ENCODER
        self.run_serve(linger=0.1)
        viewer = self.connect()
        viewer.recv(2)
        viewer.close()
        self.assertTrue(wait_until(lambda: self.started[0].poll() is not None))
        self.assertEqual(-signal.SIGKILL, self.started[0].returncode)

    def test_a_viewer_that_never_reads_is_dropped_and_the_other_keeps_receiving(self):
        self.script = FIREHOSE_ENCODER
        self.run_serve(limit_bytes=256 * 1024)
        slow = self.connect(rcvbuf=4096)
        quick = self.connect()
        received = [0]

        def drain():
            try:
                while True:
                    chunk = quick.recv(65536)
                    if not chunk:
                        return
                    received[0] += len(chunk)
            except OSError:
                return

        threading.Thread(target=drain, daemon=True).start()

        def closed_by_server():
            slow.settimeout(0.05)
            try:
                return slow.recv(1 << 20) == b""
            except socket.timeout:
                return False
            except ConnectionResetError:
                return True

        self.assertTrue(wait_until(closed_by_server, timeout=15))
        before = received[0]
        self.assertTrue(wait_until(lambda: received[0] > before + 65536))

    def test_a_ninth_viewer_is_closed_and_the_eight_stay(self):
        self.run_serve()
        eight = [self.connect() for _ in range(8)]
        for c in eight:
            self.assertEqual(b"TS", c.recv(2))
        ninth = self.connect()
        self.assertEqual(b"", ninth.recv(2))
        for c in eight:
            self.assertEqual(b"TS", c.recv(2))
        self.assertEqual(1, len(self.started))


if __name__ == "__main__":
    unittest.main()
