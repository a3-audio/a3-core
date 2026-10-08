"""The install works whether or not `aaa` is logged in (a3-core#70, #71, #72).

Without a session, `loginctl disable-linger` stopped aaa's user manager two
seconds into the REAPER download, and the `daemon-reload` before it raced the
manager linger only starts. Nothing installed an ssh server for the key the
package now asks for, and every install without a key chowned a missing ~/.ssh.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config/debian-x86_64/a3-core"
POSTINST = PACKAGE / "DEBIAN/postinst"
CONTROL = PACKAGE / "DEBIAN/control"


def function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(),
                     re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


def field(name):
    line = next(l for l in CONTROL.read_text().splitlines() if l.startswith(f"{name}:"))
    return {alt.strip().split(" ")[0]
            for entry in line.split(":", 1)[1].split(",") for alt in entry.split("|")}


class TheUserInstallRunsToTheEnd(unittest.TestCase):
    """Driven with fake loginctl, systemctl, sudo and id, never the real ones."""

    def run_user_install(self, linger_before="no"):
        with tempfile.TemporaryDirectory() as tmp:
            bin_dir = Path(tmp)
            log = bin_dir / "calls.log"
            fakes = {
                "id": "echo 1000",
                "loginctl": f'echo "loginctl $*" >> {log}\n'
                            f'[ "$1" = show-user ] && echo {linger_before}\nexit 0',
                "systemctl": f'echo "systemctl $*" >> {log}',
                "sudo": f'shift 2; "$@"',
                "env": 'while [ "${1#*=}" != "$1" ]; do shift; done; "$@"',
            }
            for name, body in fakes.items():
                (bin_dir / name).write_text(f"#!/bin/sh\n{body}\n")
                (bin_dir / name).chmod(0o755)
            subprocess.run(
                ["sh", "-c", "set -e\nAPPUSER=aaa\nUID=1000\n"
                 f"{function('user_systemctl')}\n{function('run_user_install')}\n"
                 "run_user_install"],
                env={"PATH": f"{bin_dir}:/usr/bin:/bin"},
                check=True, capture_output=True, text=True)
            return log.read_text().splitlines()

    def position(self, calls, pattern):
        for i, call in enumerate(calls):
            if re.search(pattern, call):
                return i
        self.fail(f"no call matching {pattern!r} in {calls}")

    def test_the_manager_is_up_before_it_is_asked(self):
        calls = self.run_user_install()
        self.assertLess(self.position(calls, r"^loginctl enable-linger aaa$"),
                        self.position(calls, r"^systemctl start user@1000\.service$"))
        self.assertLess(self.position(calls, r"^systemctl start user@1000\.service$"),
                        self.position(calls, r"^systemctl --user daemon-reload$"))

    def test_the_install_is_waited_for(self):
        calls = self.run_user_install()
        self.assertIn("systemctl --user start --wait a3-user-install.service", calls)

    def test_linger_goes_only_after_the_install(self):
        calls = self.run_user_install()
        self.assertLess(self.position(calls, r"start --wait a3-user-install"),
                        self.position(calls, r"^loginctl disable-linger aaa$"))

    def test_linger_that_was_on_stays_on(self):
        calls = self.run_user_install(linger_before="yes")
        self.assertNotIn("loginctl disable-linger aaa", calls)

    def test_the_postinst_runs_it(self):
        text = POSTINST.read_text()
        self.assertIsNotNone(re.search(r"(?m)^run_user_install$", text))
        outside = text.replace(function("run_user_install"), "")
        self.assertIsNone(re.search(r"(?m)^\s*loginctl (en|dis)able-linger", outside))


class AnSshServerReadsTheKey(unittest.TestCase):

    def test_openssh_server_comes_with_the_package(self):
        self.assertIn("openssh-server", field("Depends"))

    def test_nothing_conflicts_with_it(self):
        self.assertFalse({"openssh-server", "openssh-client"} & field("Conflicts"))


class AMissingSshFolderIsLeftAlone(unittest.TestCase):

    def test_ssh_is_only_chowned_when_it_exists(self):
        for line in POSTINST.read_text().splitlines():
            if re.search(r"chown .*\.ssh", line) and "install_ssh_key" not in line:
                self.assertRegex(line, r"\[ -d [^]]*\.ssh\" \] &&",
                                 f"unguarded chown of ~/.ssh: {line.strip()}")


if __name__ == "__main__":
    unittest.main()
