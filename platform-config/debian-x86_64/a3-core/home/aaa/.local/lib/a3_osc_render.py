"""The one truth, written out for what cannot read JSON.

zita's units, the postinst's default network and the beat-analyzer's conf.d
take their addresses as text. They get it from here, rendered from
a3-osc.json, so the file stays the only place a port or an IP is written.
"""

import json
import os
import shlex
from pathlib import Path

#: Where the zita units and x11vnc find their arguments (systemd's %h is the
#: home; x11vnc is a system unit and names it in full).
SYSTEMD_ENV_FILE = "%h/.config/a3/osc.env"
ENV_FILE = Path(".config/a3/osc.env")

#: The analyzer's file of ours. The beat-analyzer package reads conf.d/*.env
#: after the user's beat-analyzer.env, so this whole file is a3-core's and the
#: truth wins key by key; the user's file is never touched (2026-10-08).
ANALYZER_CONF = Path(".config/beat-analyzer/conf.d/50-a3-osc.env")
HEADER = "# rendered from a3-osc.json by a3-osc-render -- edit the truth, not this file"


def analyzer_prefix(route):
    """OSC_VU_ for the meters, OSC_HOST_ for the clock. The analyzer sends
    /vu only to its OSC_VU_ targets once it has one, so a program taking both
    on one port is named twice."""
    carries_vu = route["to"].endswith(".vu") or route.get("carries") == "vu"
    return "OSC_VU_" if carries_vu else "OSC_HOST_"


def lines(pairs):
    return "".join(f"{key}={value}\n" for key, value in pairs)


def zita_env(truth):
    j2n_host, j2n_port = truth.endpoint("radla", "zita-n2j")
    n2j = truth.listener("zita-n2j", "audio")
    return lines([
        ("A3_ZITA_J2N_HOST", j2n_host),
        ("A3_ZITA_J2N_PORT", j2n_port),
        ("A3_ZITA_N2J_HOST", truth.host(n2j["host"])),
        ("A3_ZITA_N2J_PORT", n2j["port"]),
    ])


def vnc_env(truth):
    return lines([("A3_VNC_PORT", truth.port("x11vnc", "vnc"))])


def osc_env(truth):
    """osc.env: zita's arguments and x11vnc's port."""
    return zita_env(truth) + vnc_env(truth)


def network_defaults(truth):
    network = truth.network()
    others = [port for port in network["bridge_ports"] if port != network["interface"]]
    return lines([
        ("INTERFACE", network["interface"]),
        ("ADDR", network["address"]),
        ("GATEWAY", network["gateway"]),
        ("DNS", network["dns"]),
        ("BRIDGE_WITH", shlex.quote(" ".join(others))),
    ])


def analyzer_block(truth):
    targets = []
    for route in truth.routes():
        if route.get("from") != "beat-analyzer":
            continue
        program, role = route["to"].split(".")
        host, port = truth.endpoint(program, role)
        targets.append((analyzer_prefix(route) + program, f"{host}:{port}"))
    targets.append(("OSC_PORT_A3MOTION", truth.port("beat-analyzer", "clock")))
    # Its words and the Pro DJ Link ports: beat-analyzer's Config::OscWords.
    targets += [("OSC_ADDRESS_BEAT", truth.pattern("beat")),
                ("OSC_ADDRESS_TAP", truth.pattern("tap")),
                ("OSC_ADDRESS_CLOCKMODE", truth.pattern("clockmode")),
                ("OSC_ADDRESS_VU", truth.pattern("vu")),
                ("PIONEER_PORT_ANNOUNCE", truth.port("prolink", "announce")),
                ("PIONEER_PORT_BEAT", truth.port("prolink", "beat")),
                ("PIONEER_PORT_STATUS", truth.port("prolink", "status"))]
    return f"{HEADER}\n{lines(targets)}"


def write_user_files(truth, home):
    env_file = home / ENV_FILE
    env_file.parent.mkdir(parents=True, exist_ok=True)
    env_file.write_text(osc_env(truth))
    replace_whole(home / ANALYZER_CONF, analyzer_block(truth))


def replace_whole(path, text):
    """Swap the new file in whole: Core rewrites this at every start while
    the analyzer may be reading it. The temporary name is hidden, which the
    analyzer's conf.d skips."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(text)
    os.replace(temporary, path)


def network_file(truth):
    """~/.config/a3/network.json as the installer first writes it: the
    truth's hosts and network blocks (spec truth-from-core)."""
    data = truth.data()
    return json.dumps({key: data[key] for key in ("hosts", "network")}, indent=1) + "\n"


def write_network_file_once(truth, path):
    """Create the maintainer's network file; never overwrite it. True if it
    was written."""
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("x") as handle:
            handle.write(network_file(truth))
    except FileExistsError:
        return False
    return True
