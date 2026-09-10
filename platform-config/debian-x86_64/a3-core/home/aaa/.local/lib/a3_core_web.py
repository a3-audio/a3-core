"""A window onto what Core is passing about.

Serves one page, a JSON snapshot, and a stream of snapshots at a fixed rate.
It knows nothing about Core: it is handed a Traffic to read and, optionally,
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
import math
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable, Dict, Optional, Tuple

from pythonosc.osc_message_builder import BuildError  # type: ignore

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
    needs. A non-finite float gets the same treatment: `json.dumps` would
    otherwise emit a bare `NaN`/`Infinity`/`-Infinity`, which is not JSON --
    Python's own `json.loads` accepts it as an extension, but a browser's
    `JSON.parse` throws on it. `stream.onmessage` calls `JSON.parse`
    unguarded and never evicts old rows, so one such value would throw on
    every tick from then on, with the connection itself never failing --
    a frozen table that looks live. Turning it into its repr here is what
    actually keeps the whole page from failing on one odd message.
    """
    if isinstance(value, float):
        return value if math.isfinite(value) else repr(value)
    if isinstance(value, (str, int, bool)) or value is None:
        return value
    return repr(value)


def _display_type(type_name: str) -> str:
    """The type name shown in the table, collapsing one NumPy subtype.

    `numpy.float64` subclasses `float` and serialises exactly like one --
    Core only produces it because values bound for REAPER pass through
    `np.interp`. The type column exists to tell a string "1" (the mixer's
    momentary edge) from a float 1.0 (Motion's state); `float64` sitting
    beside `float` beside `str` is noise in the one column this design
    leans on hardest, not a message class of its own the way str vs float
    actually is. The value itself is untouched -- only this label changes.
    """
    return "float" if type_name == "float64" else type_name


def as_json(snapshot: Dict[str, Any],
            previous: Optional[Dict[str, Any]]) -> Dict[str, Any]:
    """A snapshot, ready for the browser, with a rate per row.

    The rate is made here rather than in Traffic because it is the reader's
    arithmetic: two counts and the seconds between them. A row with no
    previous count -- the first look, or an address seen for the first time
    -- gets None rather than 0. Zero would read as "nothing is arriving",
    which is a much louder claim than "not known yet", and this window is for
    telling those two apart.

    `full` tells the page whether `history` is the complete ring or only
    what happened since `previous`. `_stream()` calls `traffic.snapshot()`
    with `history_since=None` exactly when it has no `previous` to give a
    cutoff from -- the first tick of a connection -- so `previous is None`
    here means the same thing `history_since is None` meant there. The page
    needs this because a dropped connection makes `EventSource` reconnect on
    its own -- ordinary behaviour, not an error -- and that reconnect's
    first event is a full ring again, same as any other connection's first
    event. Without a flag saying so, the page cannot tell "the whole
    picture" from "everything since last time" and appending the former
    duplicates every entry it already had.
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
        out["last_type"] = _display_type(row["last_type"])
        out["age"] = snapshot["at"] - row["last_seen"]

        was = before.get((row["direction"], row["address"]))
        out["rate"] = ((row["count"] - was) / span
                        if was is not None and span > 0 else None)
        rows.append(out)

    history = []
    for entry in snapshot["history"]:
        out = dict(entry)
        out["value"] = _jsonable(entry["value"])
        out["type"] = _display_type(entry["type"])
        out["age"] = snapshot["at"] - entry["at"]
        history.append(out)

    return {"at": snapshot["at"], "rows": rows, "history": history,
            "unknown_addresses": snapshot["unknown_addresses"],
            "unknown_messages": snapshot["unknown_messages"],
            "evicted": snapshot["evicted"], "full": previous is None}


def unknown_as_json(unknown: Dict[str, Any]) -> Dict[str, Any]:
    """The unknown table, ready for the browser.

    Shaped like `as_json`'s rows so the page can draw both with one function,
    with two fields fixed rather than computed: `rate` is None because there
    is no earlier fetch to compare against, and `direction` is "in" because
    Core only ever receives these -- it never sends an address it cannot
    route.

    `unknown` is carried so the page can tell a fetched row from a streamed
    one and say out loud that it is a snapshot rather than live.
    """
    rows = []
    for row in unknown["rows"]:
        out = dict(row)
        out["last_value"] = _jsonable(row["last_value"])
        out["last_type"] = _display_type(row["last_type"])
        out["age"] = unknown["at"] - row["last_seen"]
        out["rate"] = None
        out["direction"] = "in"
        out["unknown"] = True
        rows.append(out)

    return {"at": unknown["at"], "rows": rows, "evicted": unknown["evicted"]}


def parse_value(text: str) -> Any:
    """What was typed into the bench, as the type it says it is.

    `1` is a number and `"1"` is text, and in this rig that is not a detail:
    Core tells the A3 Mixer's momentary edge from A3 Motion's state by the
    argument's type (a3_core_buttons). A bench that could only send numbers
    could only exercise half the rig. A bare word is text as well, which is
    what /fx/mode wants -- `high_pass`. So is anything else that is neither
    quoted nor a parseable number, and that fallback is not neutral here: a
    string reads as the mixer's momentary edge, not as an error. A mistyped
    float (`1.2.3`) silently becomes that different message class instead of
    being refused.
    """
    said = text.strip()
    if not said:
        raise ValueError("nothing typed")

    if len(said) >= 2 and said[0] == '"' and said[-1] == '"':
        return said[1:-1]

    try:
        return int(said)
    except ValueError:
        pass
    try:
        return float(said)
    except ValueError:
        return said


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

            elif self.path == "/api/unknown":
                # Deliberately not on the stream: this can be tens of
                # thousands of rows. The page asks for it when its filter
                # switch needs it, and says on screen that it is a snapshot.
                payload = unknown_as_json(traffic.unknown_snapshot())
                self._send(200, json.dumps(payload).encode(),
                           "application/json")

            elif self.path == "/api/stream":
                self._stream()

            else:
                self._send(404, b"no such thing here", "text/plain")

        def do_POST(self):
            if self.path != "/api/send":
                self._send(404, b"no such thing here", "text/plain")
                return

            def refuse(why):
                self._send(400, json.dumps({"problem": why}).encode(),
                           "application/json")

            if send is None:
                refuse("dieses Fenster kann nur zusehen")
                return

            try:
                length = int(self.headers.get("Content-Length", "0"))
                asked = json.loads(self.rfile.read(length))
                address = str(asked["address"]).strip()
                to = str(asked["to"])
                value = parse_value(str(asked["value"]))
            except (ValueError, KeyError, TypeError) as problem:
                refuse(str(problem))
                return

            if not address.startswith("/"):
                refuse("eine Adresse fängt mit / an")
                return

            try:
                send(to, address, value)
            except KeyError:
                refuse(f"unbekanntes Ziel: {to}")
                return
            except (OSError, BuildError) as problem:
                # BuildError is pythonosc's own -- a plain Exception, not an
                # OSError -- raised for a value it cannot encode (an int
                # outside int64's range, say). Without this, the refusal
                # contract (400, nothing sent) is broken by a traceback
                # through socketserver.handle_error and no HTTP response at
                # all for exactly the input this endpoint exists to refuse.
                refuse(str(problem))
                return

            # _jsonable, not the bare value: fire() in the page parses this
            # response with response.json(), same as the stream, so a
            # non-finite value typed into the bench would break that parse
            # the same way it would have broken the stream.
            self._send(200, json.dumps({"address": address,
                                        "value": _jsonable(value)}).encode(),
                       "application/json")

        def _stream(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()

            previous = None
            try:
                while True:
                    # Only history is asked for incrementally -- rows are
                    # the addresses Core understood, about forty of them,
                    # and the page wants them whole every tick. The unknown
                    # table is not part of this loop at all: snapshot() only
                    # ever reports its size, never its rows, so it never
                    # rides this stream regardless of history_since -- see
                    # unknown_snapshot() for how a caller gets its contents.
                    # Without the history cutoff, a full ring streamed four
                    # times a second is the flood this feature exists to
                    # remove, just moved into the browser instead of the
                    # journal.
                    since = previous["at"] if previous is not None else None
                    snapshot = traffic.snapshot(history_since=since)
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
