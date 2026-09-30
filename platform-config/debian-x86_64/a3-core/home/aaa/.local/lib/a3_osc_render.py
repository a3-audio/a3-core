"""The one truth, written out for what cannot read JSON.

zita's units, the postinst's default network and the beat-analyzer's .env
take their addresses as text. They get it from here, rendered from
a3-osc.json, so the file stays the only place a port or an IP is written.
"""

import re
import shlex
from pathlib import Path

#: Where the zita units find their arguments (systemd's %h is the home).
SYSTEMD_ENV_FILE = "%h/.config/a3/osc.env"
ENV_FILE = Path(".config/a3/osc.env")

#: The analyzer's .env, a checkout on the Core. Only the block between the
#: markers is ours; the rest of the file is the maintainer's.
ANALYZER_ENV = Path("a3-system/beat-analyzer/build/.env")
BEGIN = "# >>> a3-osc: rendered from a3-osc.json by a3-osc-render -- edit the truth, not this"
END = "# <<< a3-osc"

#: Analyzer keys that are facts of the truth. Found outside the block they are
#: a second truth, and they are commented out, not deleted.
ANALYZER_KEY = re.compile(r"^(OSC_HOST_\w+|OSC_VU_\w+|OSC_PORT_A3MOTION)=")


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
    return f"{BEGIN}\n{lines(targets)}{END}\n"


def put_analyzer_block(text, block):
    """`text` with `block` in place of the old one, or appended if there is none."""
    before, found, rest = text.partition(BEGIN)
    after = rest.partition(END + "\n")[2] if found else ""
    kept = "".join(retired(line) for line in (before + after).splitlines(keepends=True))
    if kept and not kept.endswith("\n"):
        kept += "\n"
    return kept + block


def retired(line):
    return "# was: " + line if ANALYZER_KEY.match(line) else line


def write_user_files(truth, home):
    env_file = home / ENV_FILE
    env_file.parent.mkdir(parents=True, exist_ok=True)
    env_file.write_text(zita_env(truth))
    analyzer_env = home / ANALYZER_ENV
    if analyzer_env.exists():
        analyzer_env.write_text(put_analyzer_block(analyzer_env.read_text(),
                                                   analyzer_block(truth)))
