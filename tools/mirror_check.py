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

A differing file that is an older version of the package's own -- its git
history says so -- is named as behind instead: the package is right, the
machine has not caught up. It does not stop a push, and --take leaves it.

Usage:
  mirror_check.py          list what is changed here and not in the package
  mirror_check.py --take   copy the machine's version of those into the package
"""

import filecmp
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

#: What a differing machine file is. Changed here: nothing in the package's
#: history ever said this -- it has to go into the package. Behind: it is an
#: older version of the package file -- the package is right and the machine
#: has not caught up (an install never replaces a file that is there).
CHANGED_HERE = "changed here, not in the package"
BEHIND = "machine behind the package"
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


def meaning_hash(content):
    """A file's content without its comment lines and blank lines.

    What a unit or a config *says*: the machine's copy of a3-core.service was
    an old version whose comments and blank lines no commit had byte for byte
    -- compared exactly, it looked like an edit made here."""
    lines = [line.strip() for line in content.splitlines()]
    kept = [line for line in lines if line and not line.startswith(b"#")]
    return hashlib.sha1(b"\n".join(kept)).hexdigest()


def earlier_versions(package_file):
    """Every version of a package file its history has held, by meaning."""
    rel = Path(package_file).resolve().relative_to(REPO)
    commits = subprocess.run(
        ["git", "-C", str(REPO), "log", "--format=%H", "--", str(rel)],
        capture_output=True, text=True).stdout.split()
    versions = set()
    for commit in commits:
        shown = subprocess.run(
            ["git", "-C", str(REPO), "show", f"{commit}:{rel}"],
            capture_output=True)
        if shown.returncode == 0:
            versions.add(meaning_hash(shown.stdout))
    return versions


def local_changes(pairs, installed, earlier=lambda package_file: set()):
    """(package file, machine file, kind) for every machine file that differs.

    Kind is CHANGED_HERE or BEHIND. A file exactly as dpkg installed it is
    neither: the repository is merely newer, and the next install brings it.
    """
    changes = []
    for package_file, live in pairs:
        live = Path(live)
        if live.is_symlink() or not live.is_file():
            continue
        if filecmp.cmp(package_file, live, shallow=False):
            continue
        if installed.get(str(live)) == file_md5(live):
            continue
        kind = BEHIND if meaning_hash(live.read_bytes()) in earlier(package_file) \
            else CHANGED_HERE
        changes.append((Path(package_file), live, kind))
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
    """The machine's version into the package -- for what changed here only.
    A file that is behind would carry an old version back in."""
    for package_file, live, kind in changes:
        if kind == CHANGED_HERE:
            shutil.copy2(live, package_file)


def shown(path):
    return str(path).replace(os.path.expanduser("~"), "~")


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    pairs = shipped_pairs()
    sums = installed_sums(DPKG_SUMS.read_text()) if DPKG_SUMS.exists() else {}
    changes = local_changes(pairs, sums, earlier_versions)
    changed = [c for c in changes if c[2] == CHANGED_HERE]
    behind = [c for c in changes if c[2] == BEHIND]

    if "--take" in argv:
        take(changes)
        for _, live, _ in changed:
            print(f"  taken   {shown(live)}")
        for _, live, _ in behind:
            print(f"  left    {shown(live)}   (behind the package -- not taken)")
        print(f"\n  {len(changed)} file(s) copied into the package -- review and commit them.")
        return 0

    for _, live, kind in changes:
        print(f"  {kind:<34} {shown(live)}")
    for path in new_beside(pairs):
        print(f"  {'(not in the package, beside it)':<34} {shown(path)}")

    if behind:
        print(f"\n  {len(behind)} file(s) are an older version of the package's: "
              "`tools/config-status.py --install <path>` brings the machine up.")
    if changed:
        print(f"\n  {len(changed)} file(s) differ. `tools/mirror_check.py --take` copies "
              "them into the package; then commit.")
        return 1
    print("  the package mirrors this machine")
    return 0


if __name__ == "__main__":
    sys.exit(main())
