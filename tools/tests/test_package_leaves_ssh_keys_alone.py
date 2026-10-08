"""Which keys may log in belongs to the machine, not to a Core release.

The package shipped ``home/aaa/.ssh/authorized_keys``, so dpkg owned it:
an install replaced a hand-kept file with the shipped one, and
``apt remove a3-core`` deleted it, locking out a machine run over ssh
(a3-core#66).

Taking the file out of the package is not enough on its own: dpkg deletes a
file the old version shipped and the new one does not, during the upgrade.
So the preinst keeps a copy aside before the unpack and the postinst puts it
back when dpkg took it.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config/debian-x86_64/a3-core"
PREINST = PACKAGE / "DEBIAN/preinst"
POSTINST = PACKAGE / "DEBIAN/postinst"


def function(script, name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", script.read_text(),
                     re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"{script.name} defines no {name}()")
    return body.group(0)


def run(script, name, ssh_dir):
    subprocess.run(["sh", "-c", f"set -e\n{function(script, name)}\n{name} \"$1\"",
                    "sh", str(ssh_dir)], check=True, capture_output=True, text=True)


class PackageShipsNoKeys(unittest.TestCase):
    def test_nothing_is_shipped_under_ssh(self):
        shipped = [p for p in (PACKAGE / "home/aaa/.ssh").rglob("*") if p.is_file()] \
            if (PACKAGE / "home/aaa/.ssh").exists() else []
        self.assertEqual([], shipped, "dpkg owns these and deletes them with the package")


class UpgradeKeepsTheKeys(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.ssh = Path(self.tmp.name) / ".ssh"
        self.ssh.mkdir()
        self.keys = self.ssh / "authorized_keys"

    def tearDown(self):
        self.tmp.cleanup()

    def upgrade(self, dpkg_deletes=True):
        run(PREINST, "keep_authorized_keys", self.ssh)
        if dpkg_deletes and self.keys.exists():
            self.keys.unlink()
        run(POSTINST, "restore_authorized_keys", self.ssh)

    def test_keys_dpkg_deleted_come_back(self):
        self.keys.write_text("ssh-ed25519 AAAA admin\n")
        self.upgrade()
        self.assertEqual("ssh-ed25519 AAAA admin\n", self.keys.read_text())

    def test_no_copy_is_left_behind(self):
        self.keys.write_text("k\n")
        self.upgrade()
        self.assertEqual(["authorized_keys"], sorted(p.name for p in self.ssh.iterdir()))

    def test_keys_dpkg_left_alone_are_not_overwritten(self):
        self.keys.write_text("k\n")
        run(PREINST, "keep_authorized_keys", self.ssh)
        self.keys.write_text("added during the upgrade\n")
        run(POSTINST, "restore_authorized_keys", self.ssh)
        self.assertEqual("added during the upgrade\n", self.keys.read_text())
        self.assertEqual(["authorized_keys"], sorted(p.name for p in self.ssh.iterdir()))

    def test_a_machine_without_keys_gets_none(self):
        self.upgrade()
        self.assertEqual([], list(self.ssh.iterdir()))

    def test_a_machine_without_ssh_dir_is_fine(self):
        self.ssh.rmdir()
        run(PREINST, "keep_authorized_keys", self.ssh)
        run(POSTINST, "restore_authorized_keys", self.ssh)
        self.assertFalse(self.ssh.exists())


if __name__ == "__main__":
    unittest.main()
