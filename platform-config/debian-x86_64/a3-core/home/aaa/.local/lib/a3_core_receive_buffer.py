"""A receive buffer large enough for one REAPER report pass.

A pass is some 27,000 messages, about 1 MB, sent as fast as REAPER's surface
is set to send. What the buffer cannot hold while Core reads is dropped by
the kernel, and nobody is told. The system default is 208 KB; the kernel
caps a request at net.core.rmem_max, which is 4 MB on the rig's kernels.
"""

import socket

#: Room for a pass several times over.
WANTED = 4 * 1024 * 1024


def widen_receive_buffer(sock, wanted=WANTED):
    """Ask for `wanted` bytes and return what the kernel granted."""
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, wanted)
    return sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
