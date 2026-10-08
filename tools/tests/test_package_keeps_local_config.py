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


def conffile_entries():
    """(flag, path) per line; flag is "" for an ordinary conffile."""
    if not CONFFILES.exists():
        return []
    entries = []
    for line in CONFFILES.read_text().splitlines():
        fields = line.split()
        if not fields or fields[0].startswith("#"):
            continue
        entries.append(("", fields[0]) if len(fields) == 1 else (fields[0], fields[1]))
    return entries


def listed_conffiles():
    return sorted(path for flag, path in conffile_entries() if not flag)


def removed_conffiles():
    return sorted(path for flag, path in conffile_entries() if flag == "remove-on-upgrade")


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

    def test_remove_on_upgrade_is_the_only_flag(self):
        """dpkg knows no other; a typo would leave the line meaning nothing."""
        flags = {flag for flag, _ in conffile_entries() if flag}
        self.assertLessEqual(flags, {"remove-on-upgrade"})

    def test_a_conffile_to_remove_is_not_shipped(self):
        """dpkg-deb refuses to build a package that ships one."""
        shipped = set(removed_conffiles()) & set(shipped_etc_files())
        self.assertEqual(set(), shipped)

    def test_paths_are_absolute(self):
        """dpkg requires absolute paths; a relative one is ignored, which looks
        like protection and is not."""
        for _, entry in conffile_entries():
            self.assertTrue(entry.startswith("/"), f"not absolute: {entry}")


if __name__ == "__main__":
    unittest.main()
