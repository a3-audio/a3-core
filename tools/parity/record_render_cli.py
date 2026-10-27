# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records what B1's `a3-osc-render` command line says and writes, for `a3-core --render-truth` to match
(O12: after the cut no Python runs on the rig). B1 = a3-core 7a7b2441f74e. Each case runs the real
bin/a3-osc-render main(argv) in a fresh interpreter (`-I`) with a temporary HOME, because a3_osc fixes the
maintainer's network path at import; only the host name is replaced (a3nuc1, as record_render.py).

    git -C <a3-core> archive 7a7b244 platform-config/debian-x86_64/a3-core | tar -x -C <empty dir>
    A3_CORE_BASELINE=<empty dir> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_render_cli.py tests/core/data/parity/render-cli

Writes into the directory given:
  a3-osc.json          B1's shipped truth, which the cases name as A3_OSC_TRUTH
  aes67.conf.in        B1's AES67 template, which every case's home carries in the package's place
  cases.json           [{name, argv, env, exit}], env values with the @HOME@ placeholder
  <case>/before/       the home the command starts from (absent = empty)
  <case>/after/        the tree it leaves
  <case>/stdout, stderr   what it said, the temporary home written @HOME@
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

BASELINE = Path(os.environ["A3_CORE_BASELINE"])
PACKAGE = BASELINE / "platform-config/debian-x86_64/a3-core"
BIN = PACKAGE / "home/aaa/.local/bin/a3-osc-render"
TRUTH = PACKAGE / "usr/share/a3/a3-osc.json"
OUT = Path(sys.argv[1])
HOSTNAME = "a3nuc1"

DRIVER = """
import socket, sys
from importlib.machinery import SourceFileLoader
from importlib.util import module_from_spec, spec_from_loader
socket.gethostname = lambda: %r
loader = SourceFileLoader("render_bin", sys.argv[1])
module = module_from_spec(spec_from_loader("render_bin", loader))
loader.exec_module(module)
sys.exit(module.main(sys.argv[2:]))
""" % HOSTNAME

HOSTS = json.loads(TRUTH.read_text())["hosts"]
NETWORK_MOVED = json.dumps({"hosts": HOSTS, "network": {"address": "10.1.2.3/24"}})
NETWORK_NO_AUDIO = json.dumps({"hosts": HOSTS, "network": {"audio_port": ""}})
NETWORK_JOINED = (Path(__file__).resolve().parent / "data/network.json").read_text()

PACKAGE_TEMPLATE = ".local/share/a3-core/pipewire/pipewire-aes67.conf.in"
TEMPLATE = PACKAGE / "home/aaa/.local/share/a3-core/pipewire/pipewire-aes67.conf.in"

# name, argv, extra environment, files in the home before
CASES = [
    ("user-shipped", ["user"], {}, {}),
    ("user-again", ["user"], {}, {".config/a3/osc.env": "# rendered stale\n"}),
    ("user-network-refused", ["user"], {"A3_NETWORK": "@HOME@/bad.json"}, {"bad.json": "not json\n"}),
    ("user-network-joined", ["user"], {"A3_NETWORK": "@HOME@/net.json"}, {"net.json": NETWORK_JOINED}),
    ("user-no-audio", ["user"], {"A3_NETWORK": "@HOME@/net.json"}, {"net.json": NETWORK_NO_AUDIO}),
    ("network-file-first", ["network-file"], {}, {}),
    ("network-file-kept", ["network-file"], {}, {".config/a3/network.json": "mine\n"}),
    ("network-file-refused", ["network-file"], {"A3_NETWORK": "@HOME@/bad.json"}, {"bad.json": "not json\n"}),
    ("network-shipped", ["network"], {}, {}),
    ("network-moved", ["network"], {"A3_NETWORK": "@HOME@/net.json"}, {"net.json": NETWORK_MOVED}),
    ("network-refused", ["network"], {"A3_NETWORK": "@HOME@/bad.json"}, {"bad.json": "not json\n"}),
    # the parser's wording where the error is not at char 0: a trailing comma, and a later line after non-ASCII text
    ("network-refused-comma", ["network"], {"A3_NETWORK": "@HOME@/bad.json"}, {"bad.json": '{"hosts": 1,}\n'}),
    ("network-refused-lines", ["network"], {"A3_NETWORK": "@HOME@/bad.json"}, {"bad.json": '{\n "küche": 1,\n "b" 2}\n'}),
    ("usage-nothing", [], {}, {}),
    ("usage-unknown", ["bogus"], {}, {}),
    ("usage-two", ["user", "network"], {}, {}),
]


def put(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def tree(root):
    return sorted(p for p in root.rglob("*") if p.is_file())


def run_case(name, argv, extra, before):
    home = Path(tempfile.mkdtemp())
    put(home / PACKAGE_TEMPLATE, TEMPLATE.read_text())
    for relative, text in before.items():
        put(home / relative, text)
    start = {p.relative_to(home) for p in tree(home)}
    env = {"HOME": str(home), "PATH": os.environ["PATH"], "A3_OSC_TRUTH": str(TRUTH)}
    env.update({key: value.replace("@HOME@", str(home)) for key, value in extra.items()})
    done = subprocess.run(["/home/aaa/.venv/bin/python3", "-I", "-c", DRIVER, str(BIN), *argv],
                          env=env, capture_output=True, text=True)
    case = OUT / name
    put(case / "stdout", done.stdout.replace(str(home), "@HOME@"))
    put(case / "stderr", done.stderr.replace(str(home), "@HOME@"))
    for relative in sorted(start - {Path(PACKAGE_TEMPLATE)}):
        if str(relative) in before:
            put(case / "before" / relative, (home / relative).read_text())
    for path in tree(home):
        relative = path.relative_to(home)
        if str(relative) != PACKAGE_TEMPLATE:
            put(case / "after" / relative, path.read_text())
    shutil.rmtree(home)
    shown = {key: value for key, value in extra.items()}
    return {"name": name, "argv": argv, "env": shown, "exit": done.returncode}


if OUT.exists():
    shutil.rmtree(OUT)
OUT.mkdir(parents=True)
shutil.copyfile(TRUTH, OUT / "a3-osc.json")
shutil.copyfile(TEMPLATE, OUT / "aes67.conf.in")
index = [run_case(*case) for case in CASES]
put(OUT / "cases.json", json.dumps(index, indent=1) + "\n")
print("recorded", OUT)
