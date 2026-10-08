"""Which keys may log in belongs to the machine, not to a Core release.

The package shipped ``home/aaa/.ssh/authorized_keys``, so dpkg owned it:
an install replaced a hand-kept file with the shipped one, and
``apt remove a3-core`` deleted it, locking out a machine run over ssh
(a3-core#66).

Taking the file out of the package is not enough on its own: dpkg deletes a
file the old version shipped and the new one does not, during the upgrade.
So the preinst keeps a copy aside before the unpack and the postinst puts it
back when dpkg took it.

The copy lives in a root-owned folder, not beside the keys: root writing a
fixed name in a folder aaa owns follows whatever symlink aaa put there. And
the postinst puts the keys back before anything that can fail, so a failed
upgrade never leaves a machine run over ssh without its keys.
"""

import os

import re
import stat
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


def run(script, name, *args):
    call = " ".join(f'"${i}"' for i in range(1, len(args) + 1))
    return subprocess.run(["sh", "-c", f"set -e\n{function(script, name)}\n{name} {call}",
                           "sh", *map(str, args)], check=True, capture_output=True, text=True)


def top_level_commands(script):
    """The postinst's own statements, in order: function bodies and the
    here-text inside them left out."""
    text = re.sub(r"^\w+\(\) \{.*?^\}$", "", script.read_text(), flags=re.MULTILINE | re.DOTALL)
    return [line for line in text.splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


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
        self.keep = Path(self.tmp.name) / "var-lib-a3-core" / "authorized_keys.keep"
        self.owner = os.getuid()

    def tearDown(self):
        self.tmp.cleanup()

    def keep_aside(self):
        return run(PREINST, "keep_authorized_keys", self.ssh, self.keep)

    def restore(self):
        return run(POSTINST, "restore_authorized_keys", self.ssh, self.keep, self.owner)

    def upgrade(self, dpkg_deletes=True):
        self.keep_aside()
        if dpkg_deletes and self.keys.exists():
            self.keys.unlink()
        self.restore()

    def test_keys_dpkg_deleted_come_back(self):
        self.keys.write_text("ssh-ed25519 AAAA admin\n")
        self.upgrade()
        self.assertEqual("ssh-ed25519 AAAA admin\n", self.keys.read_text())

    def test_no_copy_is_left_behind(self):
        self.keys.write_text("k\n")
        self.upgrade()
        self.assertEqual(["authorized_keys"], sorted(p.name for p in self.ssh.iterdir()))
        self.assertFalse(self.keep.exists())

    def test_the_copy_is_kept_outside_the_users_folder(self):
        self.keys.write_text("k\n")
        self.keep_aside()
        self.assertEqual(["authorized_keys"], sorted(p.name for p in self.ssh.iterdir()))
        self.assertEqual("k\n", self.keep.read_text())
        self.assertEqual(0o700, stat.S_IMODE(self.keep.parent.stat().st_mode))

    def test_a_symlinked_key_file_is_not_followed(self):
        secret = Path(self.tmp.name) / "secret"
        secret.write_text("not keys\n")
        self.keys.symlink_to(secret)
        result = self.keep_aside()
        self.assertFalse(self.keep.exists())
        self.assertIn("symlink", result.stderr)

    def test_keys_are_not_put_back_through_a_symlinked_folder(self):
        self.keys.write_text("k\n")
        self.keep_aside()
        self.keys.unlink()
        self.ssh.rmdir()
        elsewhere = Path(self.tmp.name) / "elsewhere"
        elsewhere.mkdir()
        self.ssh.symlink_to(elsewhere)
        result = self.restore()
        self.assertEqual([], list(elsewhere.iterdir()))
        self.assertTrue(self.keep.exists(), "the keys must not be lost either")
        self.assertIn("symlink", result.stderr)

    def test_a_folder_dpkg_removed_comes_back_private(self):
        self.keys.write_text("k\n")
        self.keep_aside()
        self.keys.unlink()
        self.ssh.rmdir()
        self.restore()
        self.assertEqual("k\n", self.keys.read_text())
        self.assertEqual(0o700, stat.S_IMODE(self.ssh.stat().st_mode))

    def test_keys_dpkg_left_alone_are_not_overwritten(self):
        self.keys.write_text("k\n")
        self.keep_aside()
        self.keys.write_text("added during the upgrade\n")
        self.restore()
        self.assertEqual("added during the upgrade\n", self.keys.read_text())
        self.assertEqual(["authorized_keys"], sorted(p.name for p in self.ssh.iterdir()))

    def test_a_machine_without_keys_gets_none(self):
        self.upgrade()
        self.assertEqual([], list(self.ssh.iterdir()))

    def test_a_machine_without_ssh_dir_is_fine(self):
        self.ssh.rmdir()
        self.keep_aside()
        self.restore()
        self.assertFalse(self.ssh.exists())


class KeysComeBackFirst(unittest.TestCase):
    """Everything after the restore can fail under set -e (the network step,
    the config copy, the venv); the keys must be back before any of it."""

    def test_the_restore_is_the_first_thing_after_finding_the_home(self):
        commands = top_level_commands(POSTINST)
        home = next(i for i, c in enumerate(commands) if c.startswith("USER_HOME="))
        after = [c for c in commands[home + 1:] if not re.match(r"^[A-Z_]+=", c)]
        self.assertTrue(after[0].startswith("restore_authorized_keys "),
                        f"first command after USER_HOME is {after[0]!r}")

    def test_preinst_and_postinst_agree_on_where_the_copy_is(self):
        place = re.compile(r'^KEPT_KEYS="?(/var/lib/a3-core/[^"\s]+)"?$', re.MULTILINE)
        pre, post = place.search(PREINST.read_text()), place.search(POSTINST.read_text())
        self.assertIsNotNone(pre, "preinst sets no KEPT_KEYS under /var/lib/a3-core")
        self.assertIsNotNone(post, "postinst sets no KEPT_KEYS under /var/lib/a3-core")
        self.assertEqual(pre.group(1), post.group(1))


if __name__ == "__main__":
    unittest.main()
