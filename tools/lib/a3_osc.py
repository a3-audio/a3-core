"""The one truth for OSC vocabulary, ports, IPs and the network -- its reader.

Every fact about who talks to whom, on which port, with which words, lives in
/usr/share/a3/a3-osc.json (decided on 2026-09-30; the package ships it). This
module is the one place in Python that knows where that file is and how it is
laid out. Everything else asks it: `host("mixer")`, `port("motion", "vu")`,
`address("channel.volume", ch=1)`.

A missing key is an error, not a default. The point of one truth is that a fact
not written there does not exist; a silent fallback would be a second truth.
The one exception is `meters()`: the ballistics are a behaviour every display
already had before the block (2026-10-07), so an older truth reads as the
numbers the block came with (METER_DEFAULTS).
"""

import copy
import json
import os
import re
from pathlib import Path
from typing import NamedTuple

from a3_osc_join import canonical, fingerprint, join, read_network

#: Where the package puts the file. A3_OSC_TRUTH overrides it, for tests and
#: for reading a checkout's copy before it is installed.
DEFAULT_PATH = Path("/usr/share/a3/a3-osc.json")

#: The maintainer's network (spec truth-from-core, 2026-10-02): the machines'
#: addresses and Core's own interface, joined over the package's defaults.
#: $A3_NETWORK names another file -- the postinst runs as root.
NETWORK_PATH = Path.home() / ".config/a3/network.json"


class TruthError(KeyError):
    """A fact the file does not have."""


class Meters(NamedTuple):
    """How every display moves a meter bar (decided 2026-10-07)."""
    attack_ms: float
    release_db_per_second: float
    peak_hold_seconds: float


#: The numbers the block came with. A truth from before the block -- a
#: device's cache -- reads as these, not as an error: the meters are a
#: behaviour every device already had, not a fact that may be missing.
METER_DEFAULTS = Meters(attack_ms=0, release_db_per_second=20, peak_hold_seconds=1.5)

#: name -> (lowest allowed, whether the lowest itself is allowed).
_METER_FLOORS = {"attack_ms": (0, True), "release_db_per_second": (0, False),
                 "peak_hold_seconds": (0, True)}


def _meter_value(name, value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise TruthError(f"meters.{name} is not a number: {value!r}")
    floor, floor_allowed = _METER_FLOORS[name]
    if value < floor or (value == floor and not floor_allowed):
        relation = ">=" if floor_allowed else ">"
        raise TruthError(f"meters.{name} must be {relation} {floor}, is {value}")
    return value


class Truth:
    network_problem = None
    meters_problem = None

    def __init__(self, data):
        self._data = data

    def data(self):
        return copy.deepcopy(self._data)

    def canonical(self):
        return canonical(self._data)

    def fingerprint(self):
        return fingerprint(self._data)

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

    def match(self, address):
        """address() backwards: ("channel.volume", {"ch": 1}) for
        "/channel/1/volume", or None for an address the truth does not have
        (an old name, a channel out of range)."""
        for key, (regex, entry) in self._matchers().items():
            found = regex.fullmatch(address)
            if not found:
                continue
            fields = {name: int(value) for name, value in found.groupdict().items()}
            if all(entry[name][0] <= value <= entry[name][1]
                   for name, value in fields.items()):
                return key, fields
        return None

    def _matchers(self):
        if not hasattr(self, "_compiled"):
            self._compiled = {}
            for key, entry in self._data["addresses"].items():
                pattern = re.escape(entry["pattern"])
                pattern = re.sub(r"\\\{(\w+)\\\}", r"(?P<\1>\\d+)", pattern)
                self._compiled[key] = (re.compile(pattern), entry)
        return self._compiled

    def index_range(self, key, field):
        low, high = self._entry(key)[field]
        return low, high

    def vu_meters(self):
        return list(self._data["vu_meters"])

    def meters(self):
        """The meter ballistics, each missing number at its default; a
        wrong type, a value out of range or an unknown key is a TruthError."""
        block = self._data.get("meters", {})
        if not isinstance(block, dict):
            raise TruthError(f"meters is not an object: {block!r}")
        given = {key: value for key, value in block.items() if not key.startswith("_")}
        unknown = sorted(set(given) - set(Meters._fields))
        if unknown:
            raise TruthError(f"meters has unknown keys: {', '.join(unknown)}")
        merged = {**METER_DEFAULTS._asdict(), **given}
        return Meters(**{name: _meter_value(name, value) for name, value in merged.items()})

    def external(self):
        return {name: dict(block) for name, block in self._data["external"].items()}


def truth_path(path=None):
    """The file load() reads: `path`, else $A3_OSC_TRUTH, else the installed
    one. Core hashes this same file for /device/hello."""
    return Path(path or os.environ.get("A3_OSC_TRUTH") or DEFAULT_PATH)


def network_path(path=None):
    """The network file load() joins: $A3_NETWORK, else the maintainer's for
    the installed truth, else none -- an explicit truth (tests, a checkout)
    never reads the maintainer's real file by accident."""
    named = os.environ.get("A3_NETWORK")
    if named:
        return Path(named)
    if path is None and not os.environ.get("A3_OSC_TRUTH"):
        return NETWORK_PATH
    return None


def load(path=None):
    """The contract (`path`, else $A3_OSC_TRUTH, else the installed file),
    joined with the network file network_path() names. A refused network file
    leaves the package's blocks; why is in `network_problem`. A local `meters`
    block that does not pass meters() is dropped alone -- the package's
    numbers stand, the network still joins -- and why is in `meters_problem`:
    a typo on the rig must not take Core down."""
    contract = json.loads(truth_path(path).read_text())
    where = network_path(path)
    network, problem = read_network(where) if where else (None, None)
    truth = Truth(join(contract, network))
    meters_problem = None
    if network is not None and "meters" in network:
        try:
            truth.meters()
        except TruthError as refused:
            meters_problem = f"{where}: {refused.args[0]}"
            local = {key: block for key, block in network.items() if key != "meters"}
            truth = Truth(join(contract, local))
    truth.network_problem = problem
    truth.meters_problem = meters_problem
    return truth
