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

With --host the machine is another one, read over ssh: since 2026-10-08 the
workspace (and push.sh) lives on a3coreV01, while the rig that runs the
package is a3nuc1. Everything is read in one ssh call that only reads (stat,
md5sum, find, base64); only files that differ from the package come back
whole.

Usage:
  mirror_check.py                 list what is changed here and not in the package
  mirror_check.py --take          copy the machine's version of those into the package
  mirror_check.py --host a3nuc1   the same for that machine, read over ssh
"""

import argparse
import base64
import hashlib
import os
import shlex
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
    # The package ships nothing here since a3-core#66; should it ever again,
    # the machine's own keys beside it are never named, so never offered for
    # taking into a public repository.
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
        # The maintainer scripts live in packaging/DEBIAN now; the skip keeps an
        # older tree's DEBIAN/ (dpkg's, never installed as files) out too.
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


FILE, SYMLINK, MISSING, OTHER = "file", "symlink", "missing", "other"


class LocalMachine:
    """This machine's filesystem."""

    name = "this machine"

    def kind(self, path):
        path = Path(path)
        if path.is_symlink():
            return SYMLINK
        return FILE if path.is_file() else MISSING

    def md5(self, path):
        return file_md5(path)

    def read_bytes(self, path):
        return Path(path).read_bytes()

    def text(self, path):
        path = Path(path)
        return path.read_text() if path.exists() else None

    def entries(self, folder):
        folder = Path(folder)
        if not folder.is_dir():
            return []
        return [(entry, self.kind(entry) if entry.is_symlink() or entry.is_file() else OTHER)
                for entry in sorted(folder.iterdir())]

    def copy_into(self, live, package_file):
        shutil.copy2(live, package_file)


LOCAL = LocalMachine()


class HostUnreadable(Exception):
    """The other machine could not be read completely; nothing can be judged."""


def run_ssh(host, script):
    """Run a shell script on host; (exit code, stdout, stderr)."""
    done = subprocess.run(["ssh", "-o", "BatchMode=yes", host, "sh", "-s"],
                          input=script.encode(), capture_output=True)
    return (done.returncode, done.stdout.decode(errors="surrogateescape"),
            done.stderr.decode(errors="replace"))


#: Runs on the other machine. Read-only: it tests, hashes, lists and encodes.
#: A file comes back whole only when its md5 differs from the package's.
HOST_SCRIPT_HEAD = r"""set -u
f() {
  if [ -L "$2" ]; then printf 'L\t%s\n' "$1"
  elif [ -f "$2" ] && [ ! -r "$2" ]; then printf 'U\t%s\n' "$1"
  elif [ -f "$2" ]; then
    m=$(md5sum < "$2" | cut -d' ' -f1)
    printf 'F\t%s\t%s\n' "$1" "$m"
    if [ "$m" != "$3" ]; then printf 'B\t%s\t' "$1"; base64 -w0 < "$2"; printf '\n'; fi
  else printf 'M\t%s\n' "$1"; fi
}
d() {
  if [ -d "$2" ]; then find "$2" -mindepth 1 -maxdepth 1 -printf "D\t$1\t%y\t%f\n"; fi
}
"""


class HostMachine:
    """Another machine's files, read once over ssh."""

    def __init__(self, name, kinds, md5s, contents, listings, texts):
        self.name = name
        self._kinds, self._md5s, self._contents = kinds, md5s, contents
        self._listings, self._texts = listings, texts

    @classmethod
    def read(cls, host, pairs, folders, sums_path, run=None):
        """Everything the check needs from host, in one ssh call."""
        lives = list(dict.fromkeys(str(live) for _, live in pairs))
        expected = {str(live): file_md5(package_file) for package_file, live in pairs}
        folders = [str(folder) for folder in folders]
        lines = [HOST_SCRIPT_HEAD]
        lines += [f"f {i} {shlex.quote(live)} {expected[live]}" for i, live in enumerate(lives)]
        lines += [f"d {i} {shlex.quote(folder)}" for i, folder in enumerate(folders)]
        lines.append(f"if [ -f {shlex.quote(str(sums_path))} ]; then printf 'S\\t'; "
                     f"base64 -w0 < {shlex.quote(str(sums_path))}; printf '\\n'; fi")
        lines.append("printf 'E\\n'")
        code, out, err = (run or run_ssh)(host, "\n".join(lines) + "\n")
        if code != 0:
            raise HostUnreadable(err.strip() or f"ssh exited with {code}")
        return cls._parse(host, out, lives, folders, str(sums_path))

    @classmethod
    def _parse(cls, host, out, lives, folders, sums_path):
        kinds, md5s, contents, texts = {}, {}, {}, {}
        listings = {folder: [] for folder in folders}
        ended = False
        for line in out.splitlines():
            tag, _, rest = line.partition("\t")
            fields = rest.split("\t")
            if tag == "E":
                ended = True
            elif tag == "L":
                kinds[lives[int(fields[0])]] = SYMLINK
            elif tag == "M":
                kinds[lives[int(fields[0])]] = MISSING
            elif tag == "U":
                raise HostUnreadable(f"{host}: cannot read {lives[int(fields[0])]}")
            elif tag == "F":
                kinds[lives[int(fields[0])]] = FILE
                md5s[lives[int(fields[0])]] = fields[1]
            elif tag == "B":
                contents[lives[int(fields[0])]] = base64.b64decode(fields[1])
            elif tag == "D":
                folder = folders[int(fields[0])]
                kind = {"f": FILE, "l": SYMLINK}.get(fields[1], OTHER)
                listings[folder].append((Path(folder) / fields[2], kind))
            elif tag == "S":
                texts[sums_path] = base64.b64decode(fields[0]).decode()
        if not ended or len(kinds) != len(lives):
            raise HostUnreadable(f"{host}: the answer was cut off")
        for folder in listings:
            listings[folder].sort()
        return cls(host, kinds, md5s, contents, listings, texts)

    def kind(self, path):
        return self._kinds[str(path)]

    def md5(self, path):
        return self._md5s[str(path)]

    def read_bytes(self, path):
        return self._contents[str(path)]

    def text(self, path):
        return self._texts.get(str(path))

    def entries(self, folder):
        return self._listings.get(str(folder), [])

    def copy_into(self, live, package_file):
        Path(package_file).write_bytes(self.read_bytes(live))


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


def local_changes(pairs, installed, earlier=lambda package_file: set(), machine=LOCAL):
    """(package file, machine file, kind) for every machine file that differs.

    Kind is CHANGED_HERE or BEHIND. A file exactly as dpkg installed it is
    neither: the repository is merely newer, and the next install brings it.
    """
    changes = []
    for package_file, live in pairs:
        live = Path(live)
        if machine.kind(live) != FILE:
            continue
        live_md5 = machine.md5(live)
        if live_md5 == file_md5(package_file):
            continue
        if installed.get(str(live)) == live_md5:
            continue
        kind = BEHIND if meaning_hash(machine.read_bytes(live)) in earlier(package_file) \
            else CHANGED_HERE
        changes.append((Path(package_file), live, kind))
    return changes


def beside_folders(pairs, machine_root="/"):
    """The folders whose other files new_beside names."""
    machine_root = Path(machine_root)
    parents = {Path(live).parent for _, live in pairs}
    return sorted(folder for folder in parents
                  if folder.relative_to(machine_root).parts[:1] == ("home",)
                  and folder.relative_to(machine_root) not in NOT_OURS_TO_LIST)


def new_beside(pairs, machine_root="/", machine=LOCAL):
    """Machine files beside shipped ones that the package does not have."""
    shipped = {Path(live) for _, live in pairs}
    found = []
    for folder in beside_folders(pairs, machine_root):
        for entry, kind in machine.entries(folder):
            if kind == FILE and entry not in shipped and not skipped(entry.name):
                found.append(entry)
    return found


def take(changes, machine=LOCAL):
    """The machine's version into the package -- for what changed here only.
    A file that is behind would carry an old version back in."""
    for package_file, live, kind in changes:
        if kind == CHANGED_HERE:
            machine.copy_into(live, package_file)


def shown(path):
    return str(path).replace(os.path.expanduser("~"), "~")


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Is everything changed on the machine in the package?")
    parser.add_argument("--take", action="store_true",
                        help="copy the machine's version of what changed into the package")
    parser.add_argument("--host", help="check this machine over ssh instead of the local one")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    pairs = shipped_pairs()
    machine = LOCAL
    if args.host:
        try:
            machine = HostMachine.read(args.host, pairs, beside_folders(pairs), DPKG_SUMS)
        except HostUnreadable as exc:
            print(f"  ERROR: cannot read {args.host}: {exc}")
            return 2
    sums_text = machine.text(DPKG_SUMS)
    sums = installed_sums(sums_text) if sums_text else {}
    changes = local_changes(pairs, sums, earlier_versions, machine)
    changed = [c for c in changes if c[2] == CHANGED_HERE]
    behind = [c for c in changes if c[2] == BEHIND]

    if args.take:
        take(changes, machine)
        for _, live, _ in changed:
            print(f"  taken   {shown(live)}")
        for _, live, _ in behind:
            print(f"  left    {shown(live)}   (behind the package -- not taken)")
        print(f"\n  {len(changed)} file(s) copied into the package -- review and commit them.")
        return 0

    for _, live, kind in changes:
        print(f"  {kind:<34} {shown(live)}")
    for path in new_beside(pairs, machine=machine):
        print(f"  {'(not in the package, beside it)':<34} {shown(path)}")

    if behind:
        print(f"\n  {len(behind)} file(s) are an older version of the package's: "
              "`tools/config-status.py --install <path>` brings the machine up.")
    if changed:
        print(f"\n  {len(changed)} file(s) differ. `tools/mirror_check.py --take` copies "
              "them into the package; then commit.")
        return 1
    print(f"  the package mirrors {machine.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
