"""A package update must not overwrite configuration somebody edited.

dpkg only asks before replacing a file if that file is listed in
``DEBIAN/conffiles``. Without the list it treats everything under ``etc/`` as
ordinary package content and rewrites it silently on every update -- which is
what happened: local tuning came back as the shipped defaults, with no prompt
and no warning.

These tests hold the list against what the package actually ships, so a file
added under ``etc/`` cannot quietly lose that protection.
"""

import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64" / "a3-core"
CONFFILES = PACKAGE / "DEBIAN" / "conffiles"
ETC = PACKAGE / "etc"


def shipped_etc_files():
    """Every file the package installs under /etc, as an absolute install path."""
    return sorted("/" + str(p.relative_to(PACKAGE)) for p in ETC.rglob("*") if p.is_file())


def listed_conffiles():
    if not CONFFILES.exists():
        return []
    return sorted(
        line.strip()
        for line in CONFFILES.read_text().splitlines()
        if line.strip() and not line.strip().startswith("#")
    )


class PackageKeepsLocalConfig(unittest.TestCase):
    def test_conffiles_exists(self):
        self.assertTrue(
            CONFFILES.exists(),
            "DEBIAN/conffiles is missing, so dpkg overwrites every file under "
            "etc/ on update without asking",
        )

    def test_every_shipped_etc_file_is_a_conffile(self):
        missing = set(shipped_etc_files()) - set(listed_conffiles())
        self.assertEqual(
            set(),
            missing,
            "these ship under etc/ but are not listed in DEBIAN/conffiles, so an "
            "update replaces them silently: " + ", ".join(sorted(missing)),
        )

    def test_conffiles_names_nothing_the_package_does_not_ship(self):
        """A stale entry is not harmless: dpkg warns, and the list stops being
        a description of the package."""
        extra = set(listed_conffiles()) - set(shipped_etc_files())
        self.assertEqual(
            set(), extra, "listed but not shipped: " + ", ".join(sorted(extra))
        )

    def test_paths_are_absolute(self):
        """dpkg requires absolute paths; a relative one is ignored, which looks
        like protection and is not."""
        for entry in listed_conffiles():
            self.assertTrue(entry.startswith("/"), f"not absolute: {entry}")


if __name__ == "__main__":
    unittest.main()
