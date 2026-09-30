"""The one truth for OSC vocabulary, ports, IPs and the network -- its reader.

Every fact about who talks to whom, on which port, with which words, lives in
/usr/share/a3/a3-osc.json (decided on 2026-09-30; the package ships it). This
module is the one place in Python that knows where that file is and how it is
laid out. Everything else asks it: `host("mixer")`, `port("motion", "vu")`,
`address("channel.volume", ch=1)`.

A missing key is an error, not a default. The point of one truth is that a fact
not written there does not exist; a silent fallback would be a second truth.
"""

import json
import os
from pathlib import Path

#: Where the package puts the file. A3_OSC_TRUTH overrides it, for tests and
#: for reading a checkout's copy before it is installed.
DEFAULT_PATH = Path("/usr/share/a3/a3-osc.json")


class TruthError(KeyError):
    """A fact the file does not have."""


class Truth:
    def __init__(self, data):
        self._data = data

    def network(self):
        return dict(self._data["network"])

    def host(self, name):
        try:
            return self._data["hosts"][name]
        except KeyError:
            raise TruthError(f"no host '{name}' in the truth") from None

    def listeners(self):
        return [dict(listener) for listener in self._data["listeners"]]

    def listener(self, program, role):
        for listener in self._data["listeners"]:
            if listener["program"] == program and listener["role"] == role:
                return dict(listener)
        raise TruthError(f"nobody listens as {program}.{role}")

    def port(self, program, role):
        return self.listener(program, role)["port"]

    def endpoint(self, program, role):
        """(ip, port) to send to -- a wildcard listener is reached locally."""
        listener = self.listener(program, role)
        host = listener["host"]
        if host == "any":
            host = "local"
        return self.host(host), listener["port"]

    def routes(self):
        return [dict(route) for route in self._data["routes"]]

    def programs(self):
        return set(self._data["programs"])

    def addresses(self):
        return {key: dict(entry) for key, entry in self._data["addresses"].items()}

    def _entry(self, key):
        try:
            return self._data["addresses"][key]
        except KeyError:
            raise TruthError(f"no address '{key}' in the truth") from None

    def pattern(self, key):
        return self._entry(key)["pattern"]

    def address(self, key, **fields):
        """The address for `key` with its placeholders filled, e.g.
        address("channel.volume", ch=1) -> "/channel/1/volume". An index is
        checked against the range the file gives it."""
        entry = self._entry(key)
        for field, value in fields.items():
            if field in entry:
                low, high = entry[field]
                if not low <= value <= high:
                    raise TruthError(f"{key}: {field}={value} outside {low}..{high}")
        return entry["pattern"].format(**fields)

    def index_range(self, key, field):
        low, high = self._entry(key)[field]
        return low, high

    def vu_meters(self):
        return list(self._data["vu_meters"])

    def external(self):
        return {name: dict(block) for name, block in self._data["external"].items()}


def load(path=None):
    """Read the truth: `path`, else $A3_OSC_TRUTH, else the installed file."""
    if path is None:
        path = os.environ.get("A3_OSC_TRUTH", DEFAULT_PATH)
    return Truth(json.loads(Path(path).read_text()))
