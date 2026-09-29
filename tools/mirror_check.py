#!/usr/bin/env python3
"""Is everything changed on this machine in the package?

This machine is the development core: what it runs is what the package should
ship. push.sh runs this before every push and stops when the two disagree.

A shipped file counts as changed here when it differs from the package --
unless it is a symlink into the checkout (it reads the repository itself), or
exactly what dpkg installed (the repository is merely newer; the next install
brings it). dpkg's own checksums, /var/lib/dpkg/info/a3-core.md5sums, tell
the two apart: the patchbay written into dpkg's copy under ~/.local/share
differs from what dpkg installed, and that is what was lost on 2026-09-29.

Files beside shipped ones that the package does not have are named as a
warning only: not everything in ~/.config/systemd/user belongs to Core.

Usage:
  mirror_check.py          list what is changed here and not in the package
  mirror_check.py --take   copy the machine's version of those into the package
"""

import filecmp
import hashlib
import os
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PACKAGE = REPO / "platform-config" / "debian-x86_64" / "a3-core"
DPKG_SUMS = Path("/var/lib/dpkg/info/a3-core.md5sums")

CONFIG = Path("home/aaa/.local/share/a3-core/config")
LIVE_CONFIG = Path("home/aaa/.config")

#: Files the applications own -- scan caches, window positions, recent lists.
#: Never compared, never carried: shipping them is how a February plugin scan
#: reached a September machine and took the IEM plugins out of REAPER.
NEVER = (
    "reaper-vstplugins64.ini", "reaper-clap-", "reaper-fxtags.ini",
    "reaper-wndpos.ini", "reaper-recentfx.ini", "reaper-jsfx.ini",
    "reaper-defpresets.ini", "reaper-extstate.ini", "reaper-midihw",
    "reaper-themeconfig.ini", "reaper-mouse.ini", "reaper.ini",
    "__pycache__", ".pyc",
    # QjackCtl stores window geometry and per-device levels in the same file
    # as its settings, and rewrites it on exit.
    "QjackCtl.conf",
    # REAPER's own OSC patterns, beside ours, and what it writes about itself:
    # the registration, the install revision, its plug-in shell scan.
    "Default.ReaperOSC", "LogicPad.ReaperOSC", "LogicTouch.ReaperOSC",
    "reaper-reginfo2.ini", "reaper-install-rev.txt", "reaper-configzip-info",
    "reaper-vstshells64.ini",
)

#: Directories that hold shipped files among everything else a home holds.
#: Nothing beside a file directly in these is ours to name.
NOT_OURS_TO_LIST = (
    Path("home/aaa"), Path("home/aaa/.config"), Path("home/aaa/.local"),
    Path("home/aaa/.local/share"),
    # The machine's own keys sit beside the shipped authorized_keys: never
    # named, so never offered for taking into a public repository.
    Path("home/aaa/.ssh"),
)


def skipped(path):
    return any(marker in str(path) for marker in NEVER)


def shipped_pairs(package_root=PACKAGE, machine_root="/"):
    """(package file, machine file) for every file the package installs.

    config/* lands twice: dpkg's copy under ~/.local/share, and the copy
    postinst makes into ~/.config -- which is the one the programs read.
    """
    package_root, machine_root = Path(package_root), Path(machine_root)
    pairs = []
    for path in sorted(package_root.rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        rel = path.relative_to(package_root)
        if rel.parts[0] == "DEBIAN" or skipped(rel):
            continue
        pairs.append((path, machine_root / rel))
        if CONFIG in rel.parents:
            pairs.append((path, machine_root / LIVE_CONFIG / rel.relative_to(CONFIG)))
    return pairs


def installed_sums(md5sums_text, machine_root="/"):
    """dpkg's `md5  relative/path` lines as {machine path: md5}."""
    sums = {}
    for line in md5sums_text.splitlines():
        digest, _, rel = line.strip().partition("  ")
        if rel:
            sums[str(Path(machine_root) / rel)] = digest
    return sums


def file_md5(path):
    return hashlib.md5(Path(path).read_bytes()).hexdigest()


def local_changes(pairs, installed):
    """The pairs whose machine file is changed here and not in the package."""
    changes = []
    for package_file, live in pairs:
        live = Path(live)
        if live.is_symlink() or not live.is_file():
            continue
        if filecmp.cmp(package_file, live, shallow=False):
            continue
        if installed.get(str(live)) == file_md5(live):
            continue
        changes.append((Path(package_file), live))
    return changes


def new_beside(pairs, machine_root="/"):
    """Machine files beside shipped ones that the package does not have."""
    machine_root = Path(machine_root)
    shipped = {Path(live) for _, live in pairs}
    folders = {live.parent for live in shipped
               if live.parent.relative_to(machine_root).parts[:1] == ("home",)
               and live.parent.relative_to(machine_root) not in NOT_OURS_TO_LIST}
    found = []
    for folder in sorted(folders):
        if not folder.is_dir():
            continue
        for entry in sorted(folder.iterdir()):
            if entry.is_file() and not entry.is_symlink() \
                    and entry not in shipped and not skipped(entry.name):
                found.append(entry)
    return found


def take(changes):
    for package_file, live in changes:
        shutil.copy2(live, package_file)


def shown(path):
    return str(path).replace(os.path.expanduser("~"), "~")


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    pairs = shipped_pairs()
    sums = installed_sums(DPKG_SUMS.read_text()) if DPKG_SUMS.exists() else {}
    changes = local_changes(pairs, sums)

    if "--take" in argv:
        take(changes)
        for package_file, live in changes:
            print(f"  taken   {shown(live)}")
        print(f"\n  {len(changes)} file(s) copied into the package -- review and commit them.")
        return 0

    for _, live in changes:
        print(f"  changed here, not in the package   {shown(live)}")
    for path in new_beside(pairs):
        print(f"  (not in the package, beside it)    {shown(path)}")

    if changes:
        print(f"\n  {len(changes)} file(s) differ. `tools/mirror_check.py --take` copies "
              "them into the package; then commit.")
        return 1
    print("  the package mirrors this machine")
    return 0


if __name__ == "__main__":
    sys.exit(main())
