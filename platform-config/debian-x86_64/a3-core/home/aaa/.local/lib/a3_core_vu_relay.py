"""The analyzer's meters, passed on to a Motion that is not the rig's own.

The beat-analyzer sends its /vu bundles straight to fixed targets from its
build/.env -- the rig's Motion, the desk, radla. A Motion on another machine
is none of them, so the analyzer also sends them to Core's vu-relay port
(`OSC_VU_core=127.0.0.1:<core.vu-relay port>`, which Core renders into the
analyzer's block from the truth's route at its start), and Core forwards
each packet to the Motion it follows (spec devices-and-remote-access).

Bytes in, the same bytes out: nothing is parsed, so nothing here can be
slower than the socket. A port and a thread of its own, so 25 bundles a
second never queue in front of the desk's commands on Core's control port.
"""

import socket

#: Larger than any datagram the analyzer sends; a bundle of 50 meters is
#: under 2 KB.
DATAGRAM = 65535


def forward(receive, send, destination, running):
    """Each packet `receive()` returns goes to `destination()` as it stands
    at that moment, or nowhere when that is None.

    A failed send is not the end: a remote Motion that has gone away shows up
    as an error on a later send, and the next one to arrive needs its meters."""
    while running():
        data = receive()
        to = destination()
        if to is None:
            continue
        try:
            send(data, to)
        except OSError:
            pass


def relay(inbox, destination, running=lambda: True):
    """`forward` on a real socket. The packets leave by a socket of their own:
    the inbox is bound to loopback, and loopback cannot reach another machine."""
    out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        forward(lambda: inbox.recv(DATAGRAM), out.sendto, destination, running)
    finally:
        out.close()
