"""The truth joined from two parts (spec truth-from-core, 2026-10-02).

The contract -- addresses, ports, who sends what -- comes with the a3-core
package; the network -- the machines' addresses, Core's own interface -- is
the maintainer's, in ~/.config/a3/network.json. Joined, the truth has one
fingerprint every device can compare. Pure but for reading the file.
"""

import hashlib
import json

NETWORK_KEYS = ("hosts", "network")

#: Blocks the maintainer's file may carry beside the network, joined key by
#: key the same way: the meter ballistics, tuned on the rig without a package
#: (decided 2026-10-07). a3_osc checks the values, not this pure join.
LOCAL_OPTIONAL_KEYS = ("meters",)


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
    keys = NETWORK_KEYS + tuple(k for k in LOCAL_OPTIONAL_KEYS if k in data)
    return {key: data[key] for key in keys}, None


def join(contract, network):
    """The contract with the maintainer's network put in, key by key over
    the package's blocks: a host he deleted, or one a later package adds,
    comes from the package -- a whole block replaced made Core fail to start
    on a missing host."""
    joined = dict(contract)
    if network is not None:
        for key in NETWORK_KEYS + LOCAL_OPTIONAL_KEYS:
            if key in network:
                joined[key] = _key_by_key(contract.get(key, {}), network[key])
    return joined


def _key_by_key(packaged, local):
    """`local` over `packaged`; a local value that is not an object is kept
    whole, so the reader refuses it with a reason instead of this hiding it."""
    if not isinstance(local, dict):
        return local
    return {**packaged, **local}
