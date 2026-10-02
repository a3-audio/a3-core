"""Positions: the newest wins (2026-10-02).

Core reads its port in one thread, and A3 Motion sends a channel's position
on every clock tick -- some 2,300 packets a second. Under extra load the
queue filled: a desk command waited behind thousands of stale positions
("laggy from touch to sound") and packets were dropped (78,000 in half an
hour). A position is worth nothing once a newer one has arrived, so Core
takes everything waiting and handles, per position word, only the last one;
every other packet is handled as it came, in order.

Pure but for drain(), which reads a socket without waiting.
"""

import select
import socket
import traceback

from pythonosc.osc_bundle import OscBundle
from pythonosc.osc_message import OscMessage


def _addresses(data):
    if OscBundle.dgram_is_bundle(data):
        out = []
        for content in OscBundle(data):
            if isinstance(content, OscBundle):
                out += _addresses(content.dgram)
            else:
                out.append(content.address)
        return out
    return [OscMessage(data).address]


def position_key(data, is_position):
    """The position words a packet carries, if it carries nothing else --
    e.g. Motion's bundle of one channel's azimuth and elevation -- else None.
    A packet that does not parse is None: the dispatcher deals with it."""
    try:
        addresses = _addresses(data)
    except Exception:  # noqa: BLE001 -- any damage: not ours to judge here
        return None
    if not addresses or not all(is_position(a) for a in addresses):
        return None
    return tuple(sorted(addresses))


def latest_wins(packets, key_of):
    """`packets` as they arrived, (data, client); per position key only the
    last one, everything else in its place."""
    keys = [key_of(data) for data, _ in packets]
    last = {k: i for i, k in enumerate(keys) if k is not None}
    return [p for i, (p, k) in enumerate(zip(packets, keys)) if k is None or last[k] == i]


def drain(sock, limit):
    """Up to `limit` waiting datagrams as (data, client), without waiting."""
    packets = []
    while len(packets) < limit:
        try:
            packets.append(sock.recvfrom(65536, socket.MSG_DONTWAIT))
        except BlockingIOError:
            break
    return packets


def serve(sock, handle, key_of, limit, report, running, tick=None, tick_seconds=0.1):
    """Core's main loop: wait for the socket, take what waits, hand on what
    latest_wins keeps. A handler that raises is reported and the loop goes
    on, as serve_forever's did -- one bad packet must not stop Core.

    `tick`, if given, runs on this same thread after every pass and at the
    latest every `tick_seconds` while nothing arrives: work that waits for
    quiet needs no second thread on Core's state."""
    while running():
        select.select([sock], [], [], tick_seconds if tick else None)
        for data, client in latest_wins(drain(sock, limit), key_of):
            try:
                handle(data, client)
            except Exception:  # noqa: BLE001 -- reported, never fatal
                report(traceback.format_exc())
        if tick:
            try:
                tick()
            except Exception:  # noqa: BLE001 -- reported, never fatal
                report(traceback.format_exc())
