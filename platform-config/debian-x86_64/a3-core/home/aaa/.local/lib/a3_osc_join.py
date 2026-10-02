"""The truth joined from two parts (spec truth-from-core, 2026-10-02).

The contract -- addresses, ports, who sends what -- comes with the a3-core
package; the network -- the machines' addresses, Core's own interface -- is
the maintainer's, in ~/.config/a3/network.json. Joined, the truth has one
fingerprint every device can compare. Pure but for reading the file.
"""

import hashlib
import json

NETWORK_KEYS = ("hosts", "network")


def canonical(data):
    """The bytes every device hashes alike: sorted keys, no spaces, UTF-8."""
    return json.dumps(data, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False).encode("utf-8")


def fingerprint(data):
    return hashlib.sha256(canonical(data)).hexdigest()


def read_network(path):
    """(the blocks, None), (None, None) if there is no file, or (None, why)."""
    try:
        text = path.read_text()
    except FileNotFoundError:
        return None, None
    except OSError as problem:
        return None, f"{path}: {problem}"
    try:
        data = json.loads(text)
    except json.JSONDecodeError as problem:
        return None, f"{path} is not JSON: {problem}"
    if not isinstance(data, dict):
        return None, f"{path} is not an object"
    for key in NETWORK_KEYS:
        if not isinstance(data.get(key), dict):
            return None, f"{path} has no '{key}' object"
    return {key: data[key] for key in NETWORK_KEYS}, None


def join(contract, network):
    """The contract with the maintainer's network blocks put in."""
    joined = dict(contract)
    if network is not None:
        joined.update({key: network[key] for key in NETWORK_KEYS})
    return joined
