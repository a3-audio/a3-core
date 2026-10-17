"""The one truth, written out for what cannot read JSON.

x11vnc's unit, the postinst's default network, the beat-analyzer's conf.d and
PipeWire's AES67 config take their addresses as text. They get it from here, rendered from
a3-osc.json, so the file stays the only place a port or an IP is written.
"""

import json
import os
import socket
from pathlib import Path

#: Where x11vnc and the screencast find their ports (x11vnc is a system unit: it
#: names the home in full; %h would be root's).
ENV_FILE = Path(".config/a3/osc.env")

#: The analyzer's file of ours. The beat-analyzer package reads conf.d/*.env
#: after the user's beat-analyzer.env, so this whole file is a3-core's and the
#: truth wins key by key; the user's file is never touched (2026-10-08).
ANALYZER_CONF = Path(".config/beat-analyzer/conf.d/50-a3-osc.env")
HEADER = "# rendered from a3-osc.json by a3-osc-render -- edit the truth, not this file"

#: PipeWire's AES67 instance (a3-aes67.service): its config, rendered from the
#: package's template with the audio socket and this machine's name.
AES67_CONF = Path(".config/pipewire/pipewire-aes67.conf")
AES67_TEMPLATE = (Path(__file__).resolve().parent.parent
                  / "share/a3-core/pipewire/pipewire-aes67.conf.in")


def analyzer_prefix(route):
    """OSC_VU_ for the meters, OSC_HOST_ for the clock. The analyzer sends
    /vu only to its OSC_VU_ targets once it has one, so a program taking both
    on one port is named twice."""
    carries_vu = route["to"].endswith(".vu") or route.get("carries") == "vu"
    return "OSC_VU_" if carries_vu else "OSC_HOST_"


def lines(pairs):
    return "".join(f"{key}={value}\n" for key, value in pairs)


def vnc_env(truth):
    return lines([("A3_VNC_PORT", truth.port("x11vnc", "vnc"))])


def screencast_env(truth):
    return lines([("A3_SCREENCAST_PORT", truth.port("screencast", "stream"))])


def osc_env(truth):
    """osc.env: the ports of x11vnc and the screencast."""
    return vnc_env(truth) + screencast_env(truth)


def network_defaults(truth):
    network = truth.network()
    return lines([
        ("INTERFACE", network["interface"]),
        ("ADDR", network["address"]),
        ("GATEWAY", network["gateway"]),
        ("DNS", network["dns"]),
        ("AUDIO_PORT", network["audio_port"]),
        ("AUDIO_ADDR", network["audio_address"]),
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


def aes67_conf(truth, hostname, template=AES67_TEMPLATE):
    """The AES67 config for this machine, or None without an audio socket:
    AES67 and PTP run on the audio network only."""
    port = truth.network().get("audio_port", "")
    if not port:
        return None
    stream_ip, stream_port = truth.endpoint("aes67", "stream")
    sap_ip, sap_port = truth.endpoint("aes67", "sap")
    text = Path(template).read_text()
    for placeholder, filled in (("@AUDIO_PORT@", port),
                                ("@SENDER_NAME@", f"AES67 {hostname} out"),
                                ("@STREAM_IP@", stream_ip),
                                ("@STREAM_PORT@", str(stream_port)),
                                ("@SAP_IP@", sap_ip),
                                ("@SAP_PORT@", str(sap_port))):
        text = text.replace(placeholder, filled)
    return f"{HEADER}\n{text}"


def is_rendered(path):
    try:
        with path.open() as handle:
            return handle.readline().rstrip("\n") == HEADER
    except OSError:
        return False


def write_aes67_conf(truth, home, hostname=None):
    """Render the AES67 config into PipeWire's folder; what happened, said.
    A config written by hand is never replaced: it stays and is named. Without
    an audio socket a rendered one goes, so a3-aes67 does not start."""
    path = home / AES67_CONF
    text = aes67_conf(truth, hostname or socket.gethostname())
    if path.exists() and not is_rendered(path):
        return f"kept ~/{AES67_CONF}: not rendered by a3-osc-render"
    if text is None:
        if path.exists():
            path.unlink()
            return f"removed ~/{AES67_CONF}: no audio port in the network"
        return "no audio port in the network: no AES67"
    replace_whole(path, text)
    return f"rendered ~/{AES67_CONF}"


def write_user_files(truth, home):
    """osc.env, the analyzer's conf.d file and the AES67 config; what the
    AES67 step did is returned, to be said."""
    env_file = home / ENV_FILE
    env_file.parent.mkdir(parents=True, exist_ok=True)
    env_file.write_text(osc_env(truth))
    replace_whole(home / ANALYZER_CONF, analyzer_block(truth))
    return write_aes67_conf(truth, home)


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
