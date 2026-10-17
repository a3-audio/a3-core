"""One encoder, several viewers: the screencast's TCP fan-out.

OBS and the pro-DJ app both want the picture. A second encode would cost a
second share of the core the audio units leave free, so one encoder writes
MPEG-TS to its stdout and this loop copies it to every connected viewer.

A viewer that falls behind (Wi-Fi) is closed once its backlog passes the
limit: waiting for it would stall the encoder and with it every other viewer.
The encoder runs only while somebody watches: it starts with the first viewer
and stops a while after the last one has gone.

Standard library only and free of a3-core imports: a3-motion-ui carries a
byte-for-byte copy of this file.
"""

import argparse
import os
import selectors
import signal
import socket
import subprocess
import sys
import time

CHUNK = 64 * 1024
RESTART_AFTER = 2.0
KILL_AFTER = 2.0
TICK = 0.1


class Fanout:
    """Per-viewer backlog of bytes not yet written."""

    def __init__(self, limit_bytes=4 * 2**20, max_viewers=8):
        self._limit = limit_bytes
        self._max = max_viewers
        self._backlog = {}

    def add(self, viewer):
        if len(self._backlog) >= self._max:
            return False
        self._backlog[viewer] = bytearray()
        return True

    def feed(self, chunk):
        """Queue the chunk for everyone; return the viewers dropped for lagging."""
        dropped = []
        for viewer, backlog in list(self._backlog.items()):
            backlog += chunk
            if len(backlog) > self._limit:
                del self._backlog[viewer]
                dropped.append(viewer)
        return dropped

    def pending(self, viewer):
        return bytes(self._backlog[viewer])

    def sent(self, viewer, n):
        del self._backlog[viewer][:n]

    def remove(self, viewer):
        self._backlog.pop(viewer, None)

    def viewers(self):
        return list(self._backlog)


class EncoderGate:
    """Wanted from the first viewer until `linger` seconds after the last one."""

    def __init__(self, linger=10.0):
        self._linger = linger
        self._last_watched = None

    def wanted(self, viewer_count, now):
        if viewer_count > 0:
            self._last_watched = now
            return True
        if self._last_watched is None:
            return False
        if now - self._last_watched <= self._linger:
            return True
        self._last_watched = None
        return False


class _Encoder:
    """The running child, or the time it may be started again."""

    def __init__(self, start):
        self._start = start
        self.process = None
        self.restart_at = 0.0

    def running(self):
        return self.process is not None

    def start(self, selector):
        self.process = self._start()
        os.set_blocking(self.process.stdout.fileno(), False)
        selector.register(self.process.stdout, selectors.EVENT_READ, "encoder")

    def stop(self, selector):
        process, self.process = self.process, None
        if process is None:
            return
        selector.unregister(process.stdout)
        process.terminate()
        try:
            process.wait(KILL_AFTER)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        process.stdout.close()

    def exited(self):
        return self.process is not None and self.process.poll() is not None


def _close(selector, fanout, viewer):
    fanout.remove(viewer)
    try:
        selector.unregister(viewer)
    except (KeyError, ValueError):
        pass
    viewer.close()


def _accept(selector, fanout, listener):
    try:
        viewer, _ = listener.accept()
    except BlockingIOError:
        return
    viewer.setblocking(False)
    if not fanout.add(viewer):
        viewer.close()
        return
    selector.register(viewer, selectors.EVENT_READ, "viewer")


def _read_encoder(selector, fanout, encoder):
    try:
        chunk = os.read(encoder.process.stdout.fileno(), CHUNK)
    except BlockingIOError:
        return
    if not chunk:
        return  # EOF: exited() notices it on this tick
    for viewer in fanout.feed(chunk):
        _close(selector, fanout, viewer)


def _write(selector, fanout, viewer):
    try:
        n = viewer.send(fanout.pending(viewer))
    except BlockingIOError:
        return
    except OSError:
        _close(selector, fanout, viewer)
        return
    fanout.sent(viewer, n)


def _viewer_gone(viewer):
    """A viewer sends nothing; readable means it closed (or sent junk)."""
    try:
        return viewer.recv(CHUNK) == b""
    except BlockingIOError:
        return False
    except OSError:
        return True


def _watch_writes(selector, fanout):
    for viewer in fanout.viewers():
        events = selectors.EVENT_READ
        if fanout.pending(viewer):
            events |= selectors.EVENT_WRITE
        selector.modify(viewer, events, "viewer")


def _dispatch(selector, fanout, listener, encoder, key, events):
    if key.data == "listener":
        _accept(selector, fanout, listener)
    elif key.data == "encoder":
        _read_encoder(selector, fanout, encoder)
    elif key.fileobj in fanout.viewers():
        viewer = key.fileobj
        if events & selectors.EVENT_READ and _viewer_gone(viewer):
            _close(selector, fanout, viewer)
        elif events & selectors.EVENT_WRITE:
            _write(selector, fanout, viewer)


def _steer_encoder(selector, fanout, gate, encoder, clock):
    now = clock()
    wanted = gate.wanted(len(fanout.viewers()), now)
    if encoder.exited():
        encoder.stop(selector)
        encoder.restart_at = now + RESTART_AFTER
    if wanted and not encoder.running() and now >= encoder.restart_at:
        encoder.start(selector)
    elif not wanted and encoder.running():
        encoder.stop(selector)


def serve(listen_socket, start_encoder, clock=time.monotonic, stop=None, linger=10.0,
          limit_bytes=4 * 2**20, max_viewers=8):
    """Run until `stop` (a threading.Event) is set, or forever."""
    listen_socket.setblocking(False)
    selector = selectors.DefaultSelector()
    selector.register(listen_socket, selectors.EVENT_READ, "listener")
    fanout = Fanout(limit_bytes, max_viewers)
    gate = EncoderGate(linger)
    encoder = _Encoder(start_encoder)
    try:
        while stop is None or not stop.is_set():
            _watch_writes(selector, fanout)
            for key, events in selector.select(TICK):
                _dispatch(selector, fanout, listen_socket, encoder, key, events)
            _steer_encoder(selector, fanout, gate, encoder, clock)
    finally:
        encoder.stop(selector)
        for viewer in fanout.viewers():
            _close(selector, fanout, viewer)
        selector.close()


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=int(os.environ.get("A3_SCREENCAST_PORT", 0)))
    parser.add_argument("--linger", type=float, default=10.0)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not args.port or not command:
        parser.error("needs --port (or A3_SCREENCAST_PORT) and -- <encoder command>")

    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("0.0.0.0", args.port))
    listener.listen()
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))  # runs serve()'s cleanup
    serve(listener, lambda: subprocess.Popen(command, stdout=subprocess.PIPE),
          linger=args.linger)


if __name__ == "__main__":
    main(sys.argv[1:])
