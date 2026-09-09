"""A window onto what Core is passing about.

Serves one page, a JSON snapshot, and a stream of snapshots at a fixed rate.
It knows nothing about Core: it is handed a Traffic to read and, from Task 7,
something to send with. That is what makes it testable without opening an OSC
port.

**The rule this file lives under: it must never stop Core coming up.** Core
makes the sound; this is a convenience. A busy port, a nonsense --web-bind, a
broken page, a client that hangs up mid-stream -- none of them reaches Core.
Only the first two get a line in the journal, from start_window() itself; a
broken page just answers 500 and a client that hangs up is dropped in
silence, because neither is Core's problem to hear about. Nothing here
raises into Core's startup path, which is why start_window() returns a bool
instead of throwing.
"""

import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable, Dict, Optional, Tuple

#: How often the stream pushes. Not per message: at a hundred messages a
#: second, an event each would move the flood from the journal into the
#: browser and change nothing. Four a second is faster than an eye reads a
#: table and slow enough that the page stays still enough to read.
STREAM_HZ = 4

#: Where the page lives when nobody says otherwise: beside layout.json in the
#: package, so it ships with everything else and can be edited and reloaded.
DEFAULT_PAGE = (Path(__file__).resolve().parent.parent
                / "share/a3-core/web/index.html")

_server: Optional[ThreadingHTTPServer] = None
_thread: Optional[threading.Thread] = None


def _jsonable(value: Any) -> Any:
    """Something json.dumps will take, without losing what it was.

    OSC carries blobs and python-osc hands them over as bytes; the type is
    reported separately, so turning one into its repr loses nothing a reader
    needs and keeps the whole page from failing on one odd message.
    """
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return repr(value)


def as_json(snapshot: Dict[str, Any],
            previous: Optional[Dict[str, Any]]) -> Dict[str, Any]:
    """A snapshot, ready for the browser, with a rate per row.

    The rate is made here rather than in Traffic because it is the reader's
    arithmetic: two counts and the seconds between them. A row with no
    previous count -- the first look, or an address seen for the first time
    -- gets None rather than 0. Zero would read as "nothing is arriving",
    which is a much louder claim than "not known yet", and this window is for
    telling those two apart.
    """
    before: Dict[Tuple[str, str], int] = {}
    span = 0.0
    if previous is not None:
        span = snapshot["at"] - previous["at"]
        before = {(row["direction"], row["address"]): row["count"]
                  for row in previous["rows"]}

    rows = []
    for row in snapshot["rows"]:
        out = dict(row)
        out["last_value"] = _jsonable(row["last_value"])
        out["age"] = snapshot["at"] - row["last_seen"]

        was = before.get((row["direction"], row["address"]))
        out["rate"] = ((row["count"] - was) / span
                        if was is not None and span > 0 else None)
        rows.append(out)

    history = []
    for entry in snapshot["history"]:
        out = dict(entry)
        out["value"] = _jsonable(entry["value"])
        out["age"] = snapshot["at"] - entry["at"]
        history.append(out)

    return {"at": snapshot["at"], "rows": rows, "history": history,
            "unhandled": snapshot["unhandled"]}


def _handler_class(traffic, send: Optional[Callable], page: Path):

    class Window(BaseHTTPRequestHandler):
        # Otherwise every request writes a line to stderr, which is the flood
        # this whole feature exists to stop -- and at four snapshots a second
        # it would be a bad one.
        def log_message(self, fmt, *args):
            pass

        def _send(self, code, body, content_type):
            try:
                self.send_response(code)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                # Same disconnect _stream() catches below: the client hung
                # up before the response finished. Not Core's problem, and
                # not worth a line.
                pass

        def do_GET(self):
            if self.path == "/":
                try:
                    body = page.read_bytes()
                except OSError as problem:
                    self._send(500, str(problem).encode(), "text/plain")
                    return
                self._send(200, body, "text/html; charset=utf-8")

            elif self.path == "/api/traffic":
                payload = as_json(traffic.snapshot(), None)
                self._send(200, json.dumps(payload).encode(),
                           "application/json")

            elif self.path == "/api/stream":
                self._stream()

            else:
                self._send(404, b"no such thing here", "text/plain")

        def _stream(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()

            previous = None
            try:
                while True:
                    snapshot = traffic.snapshot()
                    payload = as_json(snapshot, previous)
                    previous = snapshot
                    self.wfile.write(
                        b"data: " + json.dumps(payload).encode() + b"\n\n")
                    self.wfile.flush()
                    time.sleep(1.0 / STREAM_HZ)
            except (BrokenPipeError, ConnectionResetError):
                # The tab was closed. Not an error, and not worth a line.
                pass

    return Window


def window_address() -> Tuple[str, int]:
    """Where the window actually ended up. Port 0 means the OS chose."""
    if _server is None:
        raise RuntimeError("no window is running")
    host, port = _server.server_address[:2]
    return host, port


def _address_from(bind: str) -> Tuple[str, int]:
    """Split a --web-bind into (host, port), or raise ValueError saying why.

    Strict about the colon on purpose. `bind.rpartition(":")` on a value
    without one yields an empty host, and an empty host means INADDR_ANY --
    so `--web-bind 9080`, meant as "the usual port", would put the window on
    every interface of a machine that sits on the show network. That is the
    one thing the localhost default exists to prevent, so it has to be said
    out loud rather than guessed at.
    """
    host, colon, port = bind.rpartition(":")
    if not colon:
        raise ValueError(f"--web-bind wants host:port, got {bind!r}")
    if not host:
        raise ValueError(
            f"--web-bind wants a host, got {bind!r} -- write 127.0.0.1:PORT, "
            f"or 0.0.0.0:PORT if every interface is really what you mean")

    number = int(port)          # ValueError on anything that is not a number
    if not 0 <= number <= 65535:
        raise ValueError(f"--web-bind port out of range: {number}")
    return host, number


def start_window(traffic, bind: str, send: Optional[Callable] = None,
                  page_path: Optional[Path] = None) -> bool:
    """Start serving, on a daemon thread. True if it is listening.

    Returns False rather than raising when the port is taken or the address
    makes no sense. Core calls this during startup, and a window that could
    not open is not a reason to leave the rig without a sound server.
    """
    global _server, _thread

    page = page_path or DEFAULT_PAGE

    try:
        host, port = _address_from(bind)
        _server = ThreadingHTTPServer(
            (host, port), _handler_class(traffic, send, page))
    except (OSError, ValueError, OverflowError) as problem:
        # OverflowError is what socket.bind() actually raises for a port
        # outside 0-65535 -- _address_from() already rejects that range, so
        # this should be unreachable, but it is kept here too: this is the
        # one call that must never raise into Core's startup, so it gets the
        # belt as well as the braces.
        print(f"the window could not open on {bind}: {problem}")
        _server = None
        return False

    _server.daemon_threads = True
    _thread = threading.Thread(target=_server.serve_forever, daemon=True)
    _thread.start()
    return True


def stop_window() -> None:
    """For the tests. Core never stops it -- it dies with the process."""
    global _server, _thread

    if _server is not None:
        _server.shutdown()
        _server.server_close()
        _server = None
    _thread = None
