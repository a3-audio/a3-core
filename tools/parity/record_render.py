# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records what B0's a3_osc_render writes, for the C++ renderer to match byte for byte (O12: after
the cut no Python runs on the rig).

    A3_CORE_BASELINE=<exported a3-core B0> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_render.py tests/core/data/parity/render

Writes into the directory given:
  texts/            the strings osc_env, analyzer_block, network_defaults, network_file give
  homes/<case>/     before/ (the home the writer starts from; absent = empty), after/ (the tree it
                    leaves), message.txt (the AES67 line write_user_files returns); hostname a3nuc1
  aes67.conf.in     the template, as shipped
  package/          the B0 package files the Python tests read: postinst, templates, x11vnc.service,
                    a3vnc.sh, the shipped a3-osc.json in file order (copies; Task 27 rewrites the live ones)
  texts/viewer-recipe-output   what a3vnc.sh printed with a fake vncviewer
"""
import copy
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

BASELINE = Path(os.environ["A3_CORE_BASELINE"])
PACKAGE = BASELINE / "platform-config/debian-x86_64/a3-core"
LIB = PACKAGE / "home/aaa/.local/lib"
sys.path.insert(0, str(LIB))
os.environ.pop("A3_NETWORK", None)
os.environ.pop("A3_OSC_TRUTH", None)

import a3_osc  # noqa: E402
import a3_osc_join  # noqa: E402
import a3_osc_render  # noqa: E402

OUT = Path(sys.argv[1])
HOSTNAME = "a3nuc1"
CONTRACT = json.loads((PACKAGE / "usr/share/a3/a3-osc.json").read_text())
SHIPPED = a3_osc.Truth(copy.deepcopy(CONTRACT))


def joined(**network):
    """The contract with a network.json joined in, as a3_osc.load does."""
    local = {"hosts": dict(CONTRACT["hosts"]), "network": {**CONTRACT["network"], **network}}
    return a3_osc.Truth(a3_osc_join.join(CONTRACT, local))


NO_AUDIO = joined(audio_port="")
MOVED = joined(address="10.1.2.3/24")


def put(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def tree(root):
    return {str(p.relative_to(root)) for p in root.rglob("*") if p.is_file()}


def run_home(name, truth, before):
    home = Path(tempfile.mkdtemp())
    for relative, text in before.items():
        put(home / relative, text)
    a3_osc_render.socket.gethostname = lambda: HOSTNAME
    message = a3_osc_render.write_user_files(truth, home)
    case = OUT / "homes" / name
    for relative, text in before.items():
        put(case / "before" / relative, text)
    for relative in sorted(tree(home)):
        target = case / "after" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(home / relative, target)
    put(case / "message.txt", message)
    shutil.rmtree(home)
    return case


if OUT.exists():
    shutil.rmtree(OUT)
OUT.mkdir(parents=True)

put(OUT / "texts/osc.env", a3_osc_render.osc_env(SHIPPED))
put(OUT / "texts/analyzer.env", a3_osc_render.analyzer_block(SHIPPED))
put(OUT / "texts/network-defaults", a3_osc_render.network_defaults(SHIPPED))
put(OUT / "texts/network-defaults-moved", a3_osc_render.network_defaults(MOVED))
put(OUT / "texts/network.json", a3_osc_render.network_file(SHIPPED))

template = PACKAGE / "home/aaa/.local/share/a3-core/pipewire/pipewire-aes67.conf.in"
shutil.copyfile(template, OUT / "aes67.conf.in")
for source, name in ((PACKAGE / "DEBIAN/postinst", "postinst"), (PACKAGE / "DEBIAN/templates", "templates"),
                     (PACKAGE / "etc/systemd/system/x11vnc.service", "x11vnc.service"),
                     (PACKAGE / "home/aaa/.local/share/a3-core/recipes/a3vnc.sh", "a3vnc.sh")):
    put(OUT / "package" / name, source.read_text())

# The viewer recipe, run for real with a fake vncviewer that prints its arguments.
shutil.copyfile(PACKAGE / "usr/share/a3/a3-osc.json", OUT / "package/a3-osc.json")
with tempfile.TemporaryDirectory() as tmp:
    fake = Path(tmp) / "vncviewer"
    fake.write_text('#!/bin/sh\necho "$@"\n')
    fake.chmod(0o755)
    env = dict(os.environ, A3_OSC_TRUTH=str(PACKAGE / "usr/share/a3/a3-osc.json"), PATH=f"{tmp}:{os.environ['PATH']}")
    done = subprocess.run(["bash", str(PACKAGE / "home/aaa/.local/share/a3-core/recipes/a3vnc.sh")],
                          env=env, capture_output=True, text=True, check=True)
put(OUT / "texts/viewer-recipe-output", done.stdout)

AES67 = ".config/pipewire/pipewire-aes67.conf"
shipped_case = run_home("shipped", SHIPPED, {})
rendered = (shipped_case / "after" / AES67).read_text()
run_home("removed", NO_AUDIO, {AES67: rendered, ".config/a3/keep.txt": "not ours\n"})
run_home("hand-written", SHIPPED, {AES67: "# my own\nsomething = 1\n"})
run_home("empty", NO_AUDIO, {})
print("recorded", OUT)
