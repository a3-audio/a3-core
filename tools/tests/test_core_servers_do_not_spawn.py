"""Core reads each OSC port with one thread, not a thread per packet.

Both servers were ThreadingOSCUDPServer: every datagram started a thread. On
the rig on 2026-09-25 the handler threads finished a little more slowly than
packets arrived, piled up to 36,806 in two hours, starved the receive loop, and
the kernel dropped 1.46 million packets on port 9000 -- the mixer and Motion
stopped reaching REAPER, and the UI lagged. REAPER's feedback reconnect sends
some 25,000 messages at once, which was 25,000 threads (see issue #51).

No handler blocks -- the one sleep in a3-core.py is in the project-save
thread, not in a handler -- so a port is read by one loop, one packet after
another, in the order they arrived. A thread per packet also let two fader
values overtake each other.
"""

import re
import threading
import time
import unittest
from pathlib import Path

from pythonosc import dispatcher as osc_dispatcher
from pythonosc import osc_server
from pythonosc.udp_client import SimpleUDPClient

CORE = (Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64"
        / "a3-core" / "home" / "aaa" / ".local" / "bin" / "a3-core.py")


class NoPortStartsAThreadPerPacket(unittest.TestCase):
    def setUp(self):
        self.source = CORE.read_text()

    def test_no_threading_server_is_left(self):
        self.assertNotIn("ThreadingOSCUDPServer", self.source)

    def test_both_ports_are_read_by_a_blocking_server(self):
        self.assertEqual(
            2, len(re.findall(r"osc_server\.BlockingOSCUDPServer\(", self.source)),
            "the command port and the REAPER feedback port")

    def test_no_handler_sleeps(self):
        """A blocking server stalls its whole port if a handler sleeps. The
        only sleep allowed is the project-save thread's."""
        sleeps = [m.start() for m in re.finditer(r"time\.sleep\(", self.source)]
        self.assertEqual(1, len(sleeps))
        before = self.source[:sleeps[0]]
        self.assertIn("def save_project_when_due", before[-400:])


class OnePortOneLoop(unittest.TestCase):
    """What the change buys, shown on the library itself: every message of a
    burst arrives, in order, and the thread count does not follow the burst."""

    def test_a_burst_arrives_in_order_without_new_threads(self):
        received = []
        dispatcher = osc_dispatcher.Dispatcher()
        dispatcher.map("/n", lambda _address, value: received.append(value))
        server = osc_server.BlockingOSCUDPServer(("127.0.0.1", 0), dispatcher)
        port = server.server_address[1]
        loop = threading.Thread(target=server.serve_forever, daemon=True)
        loop.start()
        try:
            threads_before = threading.active_count()
            peak = threads_before
            client = SimpleUDPClient("127.0.0.1", port)
            for n in range(200):
                client.send_message("/n", n)
                peak = max(peak, threading.active_count())
            deadline = time.monotonic() + 5.0
            while len(received) < 200 and time.monotonic() < deadline:
                time.sleep(0.01)
        finally:
            server.shutdown()
            server.server_close()

        self.assertEqual(list(range(200)), received)
        self.assertEqual(threads_before, peak)


if __name__ == "__main__":
    unittest.main()
