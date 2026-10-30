#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Every version of a user unit or drop-in the deployment once copied into
~/.config/systemd/user, by sha256, from this repository's git history.

Until 2026-10-31 the a3-core package shipped its user units under the user's
home and the postinst copied them into ~/.config, where they hid the packaged
units. Since then every unit ships under /usr/lib/systemd/user, and the
postinst moves a copy in ~/.config aside -- but only one whose checksum is one
of these: a file the deployment put there, not one changed by hand. The list
is written into the postinst (units_before_one_place); a test holds the two
equal.

    units_before_one_place.py              "<sha256>  <rel>" lines, sorted
    units_before_one_place.py --text REL   the newest version of REL in git
"""

import hashlib
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

#: Where the deployment kept the units over time, newest first.
SOURCES = (
    "platform-config/debian-x86_64/a3-core/home/aaa/.local/share/a3-core/config/systemd/user",
    "platform-config/debian-x86_64/a3-core/home/aaa/.config/systemd/user",
    "platform-config/debian-x86_64/home/aaa/.config/systemd/user",
)

#: The ones that ship under /usr/lib/systemd/user now, and those merged into
#: a unit there (a3-core.service is the program's, a3-core-app's, since then).
UNITS = (
    "a3-main.service", "a3-aes67.service", "a3-screencast.service",
    "a3-bar-per-workspace.service", "a3-bar-per-workspace.service.d/program.conf",
    "a3-core.service", "a3-core.service.d/cpu.conf", "a3-core.service.d/program.conf",
    "beat-analyzer.service.d/a3-core.conf",
    "stemdeck.service.d/a3-core.conf", "stemdeck.service.d/two-deck-off.conf",
)

NO_BLOB = "0" * 40
SYMLINK = "120000"


def git(*args):
    return subprocess.run(["git", "-C", str(REPO), *args], capture_output=True,
                          check=True).stdout


def blobs():
    """{rel: [blob, ...]} for every version of UNITS in HEAD's history, newest first."""
    found = {}
    raw = git("log", "--format=", "--raw", "--no-abbrev", "--no-renames", "HEAD", "--",
              *SOURCES).decode()
    for line in raw.splitlines():
        if not line.startswith(":"):
            continue
        meta, path = line.split("\t", 1)
        old_mode, new_mode, old_blob, new_blob = meta[1:].split()[:4]
        rel = next((path[len(s) + 1:] for s in SOURCES if path.startswith(s + "/")), None)
        if rel not in UNITS:
            continue
        for mode, blob in ((new_mode, new_blob), (old_mode, old_blob)):
            if blob != NO_BLOB and mode != SYMLINK and blob not in found.get(rel, []):
                found.setdefault(rel, []).append(blob)
    return found


def manifest():
    lines = set()
    for rel, versions in blobs().items():
        for blob in versions:
            lines.add(f"{hashlib.sha256(git('cat-file', 'blob', blob)).hexdigest()}  {rel}")
    return sorted(lines, key=lambda line: (line.split("  ", 1)[1], line))


def main(argv):
    if len(argv) == 2 and argv[0] == "--text":
        versions = blobs().get(argv[1])
        if not versions:
            print(f"no version of {argv[1]} in git", file=sys.stderr)
            return 1
        sys.stdout.buffer.write(git("cat-file", "blob", versions[0]))
        return 0
    if argv:
        print(__doc__, file=sys.stderr)
        return 2
    print("\n".join(manifest()))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except (subprocess.CalledProcessError, FileNotFoundError) as error:
        print(f"units_before_one_place: {error}", file=sys.stderr)
        sys.exit(1)
