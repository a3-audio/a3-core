"""The analyzer's meters, passed on to a Motion that is not the rig's own.

The beat-analyzer sends its /vu bundles straight to fixed targets from its
config -- the rig's Motion, the desk, radla. A Motion on another machine
is none of them, so the analyzer also sends them to Core's vu-relay port
(`OSC_VU_core=127.0.0.1:<core.vu-relay port>`, which Core renders into the
analyzer's conf.d file from the truth's route at its start), and Core forwards
each packet to the remote Motion it follows and to
every desk it follows (spec devices-and-remote-access).

Bytes in, the same bytes out: nothing is parsed, so nothing here can be
slower than the socket. A port and a thread of its own, so 25 bundles a
second never queue in front of the desk's commands on Core's control port.
"""

import socket

#: Larger than any datagram the analyzer sends; a bundle of 50 meters is
#: under 2 KB.
DATAGRAM = 65535


def forward(receive, send, destinations, running):
    """Each packet `receive()` returns goes to every destination
    `destinations()` names at that moment -- the remote Motion followed and
    every desk followed --, or nowhere when it names none.

    A failed send is not the end, for this destination or the next: a desk
    that has gone away shows up as an error on a later send, and the ones
    still there need their meters."""
    while running():
        data = receive()
        for to in destinations():
            try:
                send(data, to)
            except OSError:
                pass


def relay(inbox, destinations, running=lambda: True):
    """`forward` on a real socket. The packets leave by a socket of their own:
    the inbox is bound to loopback, and loopback cannot reach another machine."""
    out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        forward(lambda: inbox.recv(DATAGRAM), out.sendto, destinations, running)
    finally:
        out.close()
