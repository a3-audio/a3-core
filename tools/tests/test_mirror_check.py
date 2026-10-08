"""The development core is mirrored in the package: nothing changed here is missing there.

**Why this exists.** On 2026-09-29 two things changed on this machine never
reached the package -- the maintainer's patchbay (under ~/.local/share, where
the next install replaced it) and the REAPER OSC bank size -- and a fresh
machine would have come up without either. push.sh now runs this before every
push and stops when the machine and the package disagree.

What counts as a local change: a file the package ships that differs from the
package here, unless it is exactly what dpkg installed (then the repository is
merely newer, and the next install brings it) or a symlink into the checkout.
"""

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import mirror_check  # noqa: E402


def md5(text):
    return hashlib.md5(text.encode()).hexdigest()


class Machine:
    """A package tree and a machine root, both in a temporary directory."""

    def __init__(self):
        self._tmp = tempfile.TemporaryDirectory()
        base = Path(self._tmp.name)
        self.package = base / "package"
        self.root = base / "machine"
        (self.package / "DEBIAN").mkdir(parents=True)
        self.root.mkdir()

    def ship(self, rel, text):
        path = self.package / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def put(self, rel, text):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def pairs(self):
        return mirror_check.shipped_pairs(self.package, self.root)

    def changes(self, installed=None):
        found = mirror_check.local_changes(self.pairs(), installed or {})
        return sorted(str(Path(live).relative_to(self.root)) for _, live, _ in found)

    def close(self):
        self._tmp.cleanup()


class MirrorCheck(unittest.TestCase):
    def setUp(self):
        self.m = Machine()

    def tearDown(self):
        self.m.close()

    def test_a_file_the_same_on_both_sides_is_no_change(self):
        self.m.ship("etc/rtirq.conf", "a")
        self.m.put("etc/rtirq.conf", "a")
        self.assertEqual(self.m.changes(), [])

    def test_a_file_edited_here_is_a_change(self):
        self.m.ship("etc/rtirq.conf", "a")
        self.m.put("etc/rtirq.conf", "edited")
        self.assertEqual(self.m.changes(), ["etc/rtirq.conf"])

    def test_config_is_compared_where_postinst_puts_it(self):
        # config/* is copied to ~/.config by postinst; that copy is the live one.
        self.m.ship("home/aaa/.local/share/a3-core/config/rncbc.org/a3-patchbay.xml", "p")
        self.m.put("home/aaa/.config/rncbc.org/a3-patchbay.xml", "mine")
        self.assertIn("home/aaa/.config/rncbc.org/a3-patchbay.xml", self.m.changes())

    def test_a_file_as_dpkg_installed_it_is_no_change_when_the_repo_is_newer(self):
        self.m.ship("etc/rtirq.conf", "newer in the repo")
        live = self.m.put("etc/rtirq.conf", "as installed")
        self.assertEqual(self.m.changes({str(live): md5("as installed")}), [])

    def test_a_dpkg_file_edited_after_the_install_is_a_change(self):
        # The patchbay case: written into dpkg's own copy, replaced by the next install.
        rel = "home/aaa/.local/share/a3-core/config/rncbc.org/a3-patchbay.xml"
        self.m.ship(rel, "shipped")
        live = self.m.put(rel, "written here")
        self.assertEqual(self.m.changes({str(live): md5("shipped")}), [rel])

    def test_a_file_missing_here_is_no_local_change(self):
        self.m.ship("etc/rtirq.conf", "a")
        self.assertEqual(self.m.changes(), [])

    def test_a_symlink_into_the_checkout_cannot_drift(self):
        shipped = self.m.ship("home/aaa/.local/bin/a3-core.py", "code")
        link = self.m.root / "home/aaa/.local/bin/a3-core.py"
        link.parent.mkdir(parents=True)
        link.symlink_to(shipped)
        self.assertEqual(self.m.changes(), [])

    def test_what_the_application_owns_is_never_compared(self):
        self.m.ship("home/aaa/.local/share/a3-core/config/rncbc.org/QjackCtl.conf", "x")
        self.m.put("home/aaa/.config/rncbc.org/QjackCtl.conf", "window moved")
        self.assertEqual(self.m.changes(), [])

    def test_debian_control_files_are_not_shipped_files(self):
        self.m.ship("DEBIAN/postinst", "#!/bin/sh")
        self.assertEqual(self.m.pairs(), [])

    def test_dpkg_sums_become_machine_paths(self):
        text = "fcc3dd94b00e8317b8fe50a2e3b0cab3  etc/default/grub\n"
        sums = mirror_check.installed_sums(text, self.m.root)
        self.assertEqual(sums, {str(self.m.root / "etc/default/grub"):
                                "fcc3dd94b00e8317b8fe50a2e3b0cab3"})

    def test_take_copies_the_machine_into_the_package(self):
        shipped = self.m.ship("etc/rtirq.conf", "a")
        self.m.put("etc/rtirq.conf", "edited")
        changes = mirror_check.local_changes(self.m.pairs(), {})
        mirror_check.take(changes)
        self.assertEqual(shipped.read_text(), "edited")
        self.assertEqual(self.m.changes(), [])

    def test_a_new_file_beside_shipped_ones_is_named(self):
        self.m.ship("home/aaa/.local/bin/a3-core.py", "x")
        self.m.put("home/aaa/.local/bin/a3-core.py", "x")
        self.m.put("home/aaa/.local/bin/a3-new-helper", "y")
        beside = mirror_check.new_beside(self.m.pairs(), self.m.root)
        self.assertEqual([str(Path(p).relative_to(self.m.root)) for p in beside],
                         ["home/aaa/.local/bin/a3-new-helper"])

    def test_nothing_beside_etc_files_is_named(self):
        # /etc/systemd/system holds the whole machine's units, not ours.
        self.m.ship("etc/systemd/system/x11vnc.service", "u")
        self.m.put("etc/systemd/system/x11vnc.service", "u")
        self.m.put("etc/systemd/system/ssh.service", "other")
        self.assertEqual(mirror_check.new_beside(self.m.pairs(), self.m.root), [])

    def test_nothing_beside_the_shipped_ssh_key_is_named(self):
        # The package ships nothing under .ssh since a3-core#66; should it
        # again, the machine's own keys beside it must never be offered.
        self.m.ship("home/aaa/.ssh/authorized_keys", "k")
        self.m.put("home/aaa/.ssh/authorized_keys", "k")
        self.m.put("home/aaa/.ssh/id_private", "secret")
        self.assertEqual(mirror_check.new_beside(self.m.pairs(), self.m.root), [])

    def test_reapers_own_files_are_never_named(self):
        self.m.ship("home/aaa/.local/share/a3-core/config/REAPER/a3.ini", "x")
        self.m.put("home/aaa/.config/REAPER/a3.ini", "x")
        for own in ("reaper-reginfo2.ini", "reaper-install-rev.txt",
                    "reaper-configzip-info", "reaper-vstshells64.ini"):
            self.m.put(f"home/aaa/.config/REAPER/{own}", "r")
        self.assertEqual(mirror_check.new_beside(self.m.pairs(), self.m.root), [])

    # ── Which side is newer (2026-09-29) ─────────────────────────────────
    # ~/.config/systemd/user/a3-core.service still carried CPUAffinity=0,
    # which the package had dropped three days earlier; --take would have put
    # it back. A machine file that is an older version of the package file is
    # behind, not changed here -- and the repository's history can tell.

    def test_a_machine_file_that_is_an_older_package_version_is_behind(self):
        self.m.ship("home/aaa/.config/systemd/user/a3-core.service", "new")
        self.m.put("home/aaa/.config/systemd/user/a3-core.service", "old")
        earlier = lambda package_file: {mirror_check.meaning_hash(b"old")}
        found = mirror_check.local_changes(self.m.pairs(), {}, earlier)
        self.assertEqual([kind for _, _, kind in found], [mirror_check.BEHIND])

    def test_a_machine_file_no_version_of_the_package_had_is_changed_here(self):
        self.m.ship("etc/rtirq.conf", "shipped")
        self.m.put("etc/rtirq.conf", "edited")
        earlier = lambda package_file: {mirror_check.meaning_hash(b"older")}
        found = mirror_check.local_changes(self.m.pairs(), {}, earlier)
        self.assertEqual([kind for _, _, kind in found], [mirror_check.CHANGED_HERE])

    def test_take_leaves_a_file_that_is_behind_alone(self):
        shipped = self.m.ship("etc/rtirq.conf", "new")
        self.m.put("etc/rtirq.conf", "old")
        earlier = lambda package_file: {mirror_check.meaning_hash(b"old")}
        mirror_check.take(mirror_check.local_changes(self.m.pairs(), {}, earlier))
        self.assertEqual(shipped.read_text(), "new")

    def test_an_old_version_with_other_comments_is_still_behind(self):
        # The real case: the machine's unit carried an older comment block
        # and blank lines no committed version had byte for byte.
        self.m.ship("home/aaa/.config/systemd/user/a3-core.service",
                    "[Service]\n# new words\nExecStart=core\n")
        self.m.put("home/aaa/.config/systemd/user/a3-core.service",
                   "[Service]\n# old words\n\nExecStart=core\nCPUAffinity=0\n")
        committed = b"[Service]\n# older words\nExecStart=core\nCPUAffinity=0\n"
        earlier = lambda package_file: {mirror_check.meaning_hash(committed)}
        found = mirror_check.local_changes(self.m.pairs(), {}, earlier)
        self.assertEqual([kind for _, _, kind in found], [mirror_check.BEHIND])

    def test_a_changed_setting_is_a_change_in_meaning(self):
        self.assertNotEqual(mirror_check.meaning_hash(b"CPUAffinity=0\n"),
                            mirror_check.meaning_hash(b"CPUAffinity=1\n"))
        self.assertEqual(mirror_check.meaning_hash(b"# a\nX=1\n\n"),
                         mirror_check.meaning_hash(b"X=1\n# b\n"))
