#!/usr/bin/env python3
"""Compare what this machine runs against what the repository ships.

The package writes into $HOME two different ways, and both are checked:

  home/aaa/<path>                              ->  ~/<path>
  home/aaa/.local/share/a3-core/config/<path>  ->  ~/.config/<path>   (postinst)

The second is a `cp -rn` in DEBIAN/postinst, so it installs but never
overwrites -- which means a file edited on the machine silently stops matching
the repository and nothing says so. That is what this exists to show.

Symlinked files are reported as `linked`: the machine reads the repository
directly, so they cannot drift.

`--install` is the other direction, and the package cannot do it: postinst
copies with `cp -rn`, which never overwrites, so a file that already exists on
the machine keeps whatever it has. That is right for configuration somebody
tuned and wrong for a fix -- the corrected Description in zita-n2j.service sat
in the package for weeks and could never arrive. So: repo -> machine, one file
at a time, with the diff shown and the old one kept.

Usage:
  config-status.py                 list what differs
  config-status.py --export        copy the machine's version into the repo
  config-status.py --export PATH   only that one repo-relative path
  config-status.py --install       copy the repo's version onto the machine
  config-status.py --install PATH  only that one repo-relative path
"""

import filecmp
import os
import shutil
import sys

HOME = os.path.expanduser("~")
REPO_ROOT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "platform-config", "debian-x86_64", "a3-core")
PKG_HOME = os.path.normpath(os.path.join(REPO_ROOT, "home", "aaa"))
CONFIG_SRC = os.path.join(PKG_HOME, ".local", "share", "a3-core", "config")

#: Files the application owns -- scan caches, window positions, recent lists.
#: Never carried either way; shipping them is how a February plugin scan
#: reached a September machine and took the IEM plugins out of REAPER.
NEVER = (
    "reaper-vstplugins64.ini", "reaper-clap-", "reaper-fxtags.ini",
    "reaper-wndpos.ini", "reaper-recentfx.ini", "reaper-jsfx.ini",
    "reaper-defpresets.ini", "reaper-extstate.ini", "reaper-midihw",
    "reaper-themeconfig.ini", "reaper-mouse.ini", "reaper.ini",
    "__pycache__", ".pyc",
    # QjackCtl stores window geometry and per-device levels in the same file
    # as its settings, and rewrites it on exit. Carrying it either way means
    # committing where somebody left a window.
    "QjackCtl.conf",
)


def skipped(path):
    return any(marker in path for marker in NEVER)


def pairs():
    """Yield (repo_path, live_path, label) for every shipped file."""
    for root, _, files in os.walk(PKG_HOME):
        for name in files:
            repo = os.path.join(root, name)
            rel = os.path.relpath(repo, PKG_HOME)
            if skipped(rel):
                continue

            # Direct: home/aaa/<rel> -> ~/<rel>
            yield repo, os.path.join(HOME, rel), rel

            # postinst also copies config/* into ~/.config/
            if repo.startswith(CONFIG_SRC + os.sep):
                under = os.path.relpath(repo, CONFIG_SRC)
                yield repo, os.path.join(HOME, ".config", under), rel


def classify(repo, live):
    if os.path.islink(live):
        real = os.path.realpath(live)
        return "linked" if real == os.path.realpath(repo) else "linked elsewhere"
    if not os.path.exists(live):
        return "not on this machine"
    if filecmp.cmp(repo, live, shallow=False):
        return "same"
    return "DIFFERS"


def main():
    export = "--export" in sys.argv
    install = "--install" in sys.argv

    if export and install:
        print("  --export and --install are opposite directions; pick one")
        return 2

    only = None
    if export or install:
        flag = "--export" if export else "--install"
        rest = [a for a in sys.argv[1:] if a != flag]
        only = rest[0] if rest else None

    # Every mapping is reported on its own. Collapsing them to the "best"
    # state hides the interesting case: a file symlinked into the checkout
    # through ~/.config while the copy dpkg installed under
    # ~/.local/share/a3-core sits months out of date. That is not a clean
    # file, and the first version of this script called it one.
    rows, differing = [], []
    for repo, live, rel in pairs():
        state = classify(repo, live)
        rows.append((rel, state, repo, live))
        if state == "DIFFERS":
            differing.append((rel, repo, live))

    for rel, state, _, live in sorted(rows):
        if state not in ("same", "linked"):
            where = live.replace(HOME, "~")
            print(f"  {state:<20} {rel}\n  {'':<20}   at {where}")

    linked = sum(1 for _, s, _, _ in rows if s == "linked")
    same = sum(1 for _, s, _, _ in rows if s == "same")
    print(f"\n  {len(rows)} install locations: {linked} linked, "
          f"{same} identical, {len(differing)} differing")

    if not (export or install):
        if differing:
            print("  --export takes the machine's version, "
                  "--install puts the repo's onto the machine")
        return 0

    for rel, repo, live in differing:
        if only and rel != only:
            continue

        if export:
            shutil.copy2(live, repo)
            print(f"  exported  {rel}")
            continue

        # Installing overwrites something a person may have tuned, so the old
        # one is kept beside it rather than trusted to a backup somebody has
        # not made.
        if os.path.islink(live):
            print(f"  skipped   {rel} -- it is a symlink into the checkout")
            continue

        keep = live + ".before-install"
        shutil.copy2(live, keep)
        shutil.copy2(repo, live)
        print(f"  installed {rel}")
        print(f"            previous kept at {keep.replace(HOME, '~')}")

    if install:
        print("\n  systemd units changed? `systemctl --user daemon-reload`")
    return 0


if __name__ == "__main__":
    sys.exit(main())
