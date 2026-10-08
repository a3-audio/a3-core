"""A new Core can be given one ssh key at install time, and only when asked.

The package no longer ships authorized_keys (a3-core#66), so a fresh Core has
no key at all. The postinst asks for one public key (a3-core/ssh-key); an
empty answer adds nothing, a valid key is appended once, anything else is
refused without failing the install. The answer is cleared after it is used,
so an upgrade never puts back a key somebody removed by hand.
"""

import re
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64" / "a3-core"
POSTINST = PACKAGE / "DEBIAN" / "postinst"
TEMPLATES = PACKAGE / "DEBIAN" / "templates"

KEY = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAbCdEfGhIjKlMnOpQrStUvWxYz0123456789abcdefg admin@desk"
OTHER = "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQC7 old@laptop"

# debconf as files in $DB, one per question; db_go answers from $ANSWER.
STUBS = r"""
db_get () { RET="$(cat "$DB/value" 2>/dev/null || true)"; }
db_set () { printf '%s' "$2" > "$DB/value"; }
db_input () { echo "input $1 $2"; }
db_go () { [ -f "$ANSWER" ] && cp "$ANSWER" "$DB/value"; return 0; }
"""


def function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(),
                     re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


def template(name):
    for block in TEMPLATES.read_text().split("\n\n"):
        if f"Template: a3-core/{name}\n" in block + "\n":
            return dict(re.findall(r"^(\w+):[ ]?(.*)$", block, re.MULTILINE))
    return None


class TheQuestion(unittest.TestCase):
    def test_it_is_a_string_that_defaults_to_empty(self):
        question = template("ssh-key")
        self.assertIsNotNone(question, "templates has no a3-core/ssh-key")
        self.assertEqual("string", question["Type"])
        self.assertEqual("", question["Default"])

    def test_the_postinst_asks_it_at_a_priority_that_is_shown(self):
        """A noninteractive install skips it at any priority; an interactive
        one only shows `high` and above by default."""
        self.assertRegex(POSTINST.read_text(), r"db_input high a3-core/ssh-key")

    def test_it_runs_after_the_kept_keys_are_restored(self):
        """Otherwise a new file would stop the old keys coming back."""
        text = POSTINST.read_text()
        restore = text.index('\nrestore_authorized_keys "')
        ask = text.index('\nssh_key_step "')
        chown = text.index('chown -R "${APPUSER}:${APPUSER}" "${USER_HOME}/.ssh"')
        self.assertLess(restore, ask)
        self.assertLess(ask, chown)


class InstallingTheKey(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.ssh = Path(self.tmp.name) / ".ssh"
        self.keys = self.ssh / "authorized_keys"

    def tearDown(self):
        self.tmp.cleanup()

    def install(self, key):
        return subprocess.run(
            ["sh", "-c", f"set -e\n{function('install_ssh_key')}\n"
             'install_ssh_key "$1" "$2"', "sh", str(self.ssh), key],
            check=True, capture_output=True, text=True)

    def test_an_empty_answer_adds_nothing(self):
        self.install("")
        self.assertFalse(self.ssh.exists())

    def test_a_key_lands_in_a_private_file(self):
        self.install(KEY)
        self.assertEqual(KEY + "\n", self.keys.read_text())
        self.assertEqual(0o700, stat.S_IMODE(self.ssh.stat().st_mode))
        self.assertEqual(0o600, stat.S_IMODE(self.keys.stat().st_mode))

    def test_existing_keys_stay(self):
        self.ssh.mkdir()
        self.keys.write_text(OTHER + "\n")
        self.install(KEY)
        self.assertEqual(f"{OTHER}\n{KEY}\n", self.keys.read_text())

    def test_a_file_without_a_last_newline_is_not_glued(self):
        self.ssh.mkdir()
        self.keys.write_text(OTHER)
        self.install(KEY)
        self.assertEqual(f"{OTHER}\n{KEY}\n", self.keys.read_text())

    def test_the_same_key_twice_is_one_line(self):
        self.install(KEY)
        self.install(KEY)
        self.assertEqual(KEY + "\n", self.keys.read_text())

    def test_surrounding_blanks_are_ignored(self):
        self.install(f"  {KEY}  ")
        self.assertEqual(KEY + "\n", self.keys.read_text())

    def test_what_is_not_a_public_key_is_refused(self):
        for bad in ("hello", "-----BEGIN OPENSSH PRIVATE KEY-----",
                    "ssh-ed25519", "ssh-ed25519 not*base64",
                    'command="sh" ' + KEY, f"{KEY}\n{OTHER}"):
            with self.subTest(bad=bad):
                result = self.install(bad)
                self.assertFalse(self.keys.exists(), bad)
                self.assertIn("not a public key", result.stderr)


class TheStep(unittest.TestCase):
    """ssh_key_step against the stubs: asks, installs, then forgets."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.db, self.answer = root / "db", root / "answer"
        self.db.mkdir()
        self.ssh = root / ".ssh"

    def tearDown(self):
        self.tmp.cleanup()

    def step(self, answer=None):
        if answer is not None:
            self.answer.write_text(answer)
        script = (f"set -e\n{STUBS}\n{function('install_ssh_key')}\n"
                  f"{function('ssh_key_step')}\n" 'ssh_key_step "$1"')
        return subprocess.run(["sh", "-c", script, "sh", str(self.ssh)], check=True,
                              capture_output=True, text=True,
                              env={"DB": str(self.db), "ANSWER": str(self.answer),
                                   "PATH": "/usr/bin:/bin"})

    def test_it_asks(self):
        self.assertIn("input high a3-core/ssh-key", self.step().stdout)

    def test_the_answer_is_installed_and_then_cleared(self):
        self.step(KEY)
        self.assertEqual(KEY + "\n", (self.ssh / "authorized_keys").read_text())
        self.assertEqual("", (self.db / "value").read_text())

    def test_no_answer_installs_nothing(self):
        self.step()
        self.assertFalse(self.ssh.exists())


if __name__ == "__main__":
    unittest.main()
