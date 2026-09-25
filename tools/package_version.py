#!/usr/bin/env python3
"""The Debian version of the a3-core package, read off the repository's tags.

Every package the workflow published was 1.0.0, because the version sat in
DEBIAN/control and nothing changed it -- and apt compares versions, not
contents, so a rig with 1.0.0 installed never saw another update. The Action
ran green and published code nobody could get (2026-09-25).

Versions are tags in this system (v03.0 is the first, set in every repo at
once), so the package is the last tag plus the commits since it: v03.0 with
63 commits on top is 03.0+63. That grows with every push, and a new tag starts
above every commit before it. No tag is an error rather than a guess: a
made-up version could sort below what a rig already has, and apt would never
offer it.

    python3 tools/package_version.py            prints the version for this repo
"""

import re
import subprocess
import sys
from pathlib import Path

DESCRIBE = re.compile(r"^v(\d[0-9A-Za-z.]*)-(\d+)-g[0-9a-f]+$")


def debian_version(describe):
    """03.0+63 from `git describe --tags --long` output like v03.0-63-g2656b74."""
    match = DESCRIBE.match(describe.strip())
    if not match:
        raise ValueError(f"not a tagged description: {describe!r}")
    tag, commits = match.groups()
    return f"{tag}+{commits}"


def version_of(repo):
    """The version for the repository at `repo`, from its newest v* tag."""
    describe = subprocess.run(
        ["git", "-C", str(repo), "describe", "--tags", "--long", "--match", "v*"],
        check=True, capture_output=True, text=True).stdout
    return debian_version(describe)


def main():
    repo = Path(__file__).resolve().parents[1]
    try:
        print(version_of(repo))
    except (ValueError, subprocess.CalledProcessError) as error:
        print(f"package_version: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
