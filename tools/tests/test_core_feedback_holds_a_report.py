"""Core's REAPER feedback port holds a whole report pass.

At start Core asks REAPER for everything (/action/41743): some 27,000
messages, about 1 MB. The port kept the system default receive buffer
(208 KB), so a REAPER sending faster than Core reads lost the overflow
without a word -- on the rig, with REAPER's surface at 8192-byte packets and
1 ms between them, 116 packets on one start, among them the mute reports of
main and main_vu, and the gate stayed shut (2026-10-14). The port now asks
for 4 MB, which holds a pass however fast REAPER sends it.
"""

import socket
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

from a3_core_receive_buffer import WANTED, widen_receive_buffer   # noqa: E402

CORE = (PACKAGE / "home/aaa/.local/bin/a3-core.py").read_text()
RMEM_MAX = int(Path("/proc/sys/net/core/rmem_max").read_text())
RMEM_DEFAULT = int(Path("/proc/sys/net/core/rmem_default").read_text())

#: A report pass as REAPER sends it at 8192-byte packets: 1.2 MB.
PACKET = 8192
PASS = 150


def _socket():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    return sock


def _arrived_of_a_pass(receiver):
    """Send a whole pass before reading any of it, as a REAPER faster than
    Core does; count what can be read afterwards."""
    sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for _ in range(PASS):
        sender.sendto(b"x" * PACKET, receiver.getsockname())
    sender.close()
    arrived = 0
    receiver.setblocking(False)
    try:
        while True:
            receiver.recv(65536)
            arrived += 1
    except BlockingIOError:
        return arrived


class TheBufferIsWidened(unittest.TestCase):
    def test_it_asks_for_four_megabytes(self):
        self.assertEqual(4 * 1024 * 1024, WANTED)

    def test_it_gets_what_the_system_allows(self):
        with _socket() as sock:
            effective = widen_receive_buffer(sock)
            self.assertEqual(
                effective, sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF))
            self.assertGreaterEqual(effective, min(WANTED, RMEM_MAX))


@unittest.skipUnless(RMEM_MAX >= WANTED, "the system caps receive buffers lower")
class APassFits(unittest.TestCase):
    @unittest.skipUnless(RMEM_DEFAULT < PACKET * PASS,
                         "the default buffer already holds a pass here")
    def test_the_default_buffer_loses_part_of_a_pass(self):
        with _socket() as sock:
            self.assertLess(_arrived_of_a_pass(sock), PASS)

    def test_a_widened_buffer_holds_all_of_it(self):
        with _socket() as sock:
            widen_receive_buffer(sock)
            self.assertEqual(PASS, _arrived_of_a_pass(sock))


class CoreWidensTheFeedbackPort(unittest.TestCase):
    def test_the_feedback_server_socket_is_widened(self):
        self.assertIn("widen_receive_buffer(feedback_server.socket)", CORE)

    def test_it_is_widened_before_the_server_reads(self):
        widened = CORE.index("widen_receive_buffer(feedback_server.socket)")
        served = CORE.index("target=feedback_server.serve_forever")
        self.assertLess(widened, served)


if __name__ == "__main__":
    unittest.main()
