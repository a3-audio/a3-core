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
import io
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

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
        self.m.ship("home/aaa/.local/share/a3-core/config/i3/config", "p")
        self.m.put("home/aaa/.config/i3/config", "mine")
        self.assertIn("home/aaa/.config/i3/config", self.m.changes())

    def test_a_file_as_dpkg_installed_it_is_no_change_when_the_repo_is_newer(self):
        self.m.ship("etc/rtirq.conf", "newer in the repo")
        live = self.m.put("etc/rtirq.conf", "as installed")
        self.assertEqual(self.m.changes({str(live): md5("as installed")}), [])

    def test_a_dpkg_file_edited_after_the_install_is_a_change(self):
        # The patchbay case (2026-09-29): written into dpkg's own copy, replaced by the next install.
        rel = "home/aaa/.local/share/a3-core/config/i3/config"
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

    def test_what_a_program_writes_for_itself_is_never_compared(self):
        self.m.ship("home/aaa/.local/share/a3-core/tools/__pycache__/x.pyc", "x")
        self.m.put("home/aaa/.local/share/a3-core/tools/__pycache__/x.pyc", "recompiled")
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


# ── The rig, read from the workspace machine (2026-10-08) ────────────────
# The workspace moved off the rig onto a3coreV01, so push.sh runs where the
# package is built, not where it runs. `--host a3nuc1` reads the rig's files
# over ssh -- in one call, read-only -- and judges them exactly as the local
# check does. The ssh layer is faked by running the same script with a local
# `sh` against the temporary machine root.

class FakeSsh:
    def __init__(self, fail=None, answer=None):
        self.calls, self.outputs = [], []
        self.fail, self.answer = fail, answer

    def __call__(self, host, script):
        self.calls.append((host, script))
        if self.fail:
            return 255, "", self.fail
        if self.answer is not None:
            return 0, self.answer, ""
        r = subprocess.run(["sh", "-s"], input=script, capture_output=True, text=True)
        self.outputs.append(r.stdout)
        return r.returncode, r.stdout, r.stderr


class MirrorCheckOnAHost(unittest.TestCase):
    def setUp(self):
        self.m = Machine()
        self.ssh = FakeSsh()
        self.sums = self.m.root / "var/lib/dpkg/info/a3-core.md5sums"

    def tearDown(self):
        self.m.close()

    def host(self, ssh=None):
        pairs = self.m.pairs()
        return mirror_check.HostMachine.read(
            "a3nuc1", pairs, mirror_check.beside_folders(pairs, self.m.root),
            self.sums, run=ssh or self.ssh)

    def found(self, earlier=lambda package_file: set()):
        host = self.host()
        text = host.text(self.sums)
        installed = mirror_check.installed_sums(text, self.m.root) if text else {}
        return mirror_check.local_changes(self.m.pairs(), installed, earlier, machine=host)

    def changes(self):
        return sorted(str(Path(live).relative_to(self.m.root)) for _, live, _ in self.found())

    def test_the_host_is_read_in_one_ssh_call(self):
        self.m.ship("etc/rtirq.conf", "a")
        self.m.ship("home/aaa/.config/a3/x.conf", "b")
        self.m.put("etc/rtirq.conf", "a")
        self.host()
        self.assertEqual([host for host, _ in self.ssh.calls], ["a3nuc1"])

    def test_the_script_writes_nothing(self):
        self.m.ship("etc/rtirq.conf", "a")
        self.host()
        script = self.ssh.calls[0][1]
        self.assertNotIn(">", script)
        for verb in ("rm ", "mv ", "cp ", "tee ", "dd ", "chmod ", "touch "):
            self.assertNotIn(verb, script)

    def test_ssh_runs_in_batch_mode_with_the_script_on_stdin(self):
        with mock.patch("mirror_check.subprocess.run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, b"E\n", b"")
            mirror_check.run_ssh("a3nuc1", "echo hi")
        argv = run.call_args.args[0]
        self.assertEqual(argv, ["ssh", "-o", "BatchMode=yes", "a3nuc1", "sh", "-s"])
        self.assertEqual(run.call_args.kwargs["input"], b"echo hi")

    def test_a_file_edited_on_the_host_is_a_change(self):
        self.m.ship("etc/rtirq.conf", "a")
        self.m.put("etc/rtirq.conf", "edited")
        self.assertEqual(self.changes(), ["etc/rtirq.conf"])

    def test_a_file_the_same_on_the_host_is_no_change_and_is_not_sent(self):
        self.m.ship("etc/rtirq.conf", "the same")
        self.m.put("etc/rtirq.conf", "the same")
        self.assertEqual(self.changes(), [])
        self.assertNotIn("\nB\t", "\n" + self.ssh.outputs[0])

    def test_a_file_missing_on_the_host_is_no_change(self):
        self.m.ship("etc/rtirq.conf", "a")
        self.assertEqual(self.changes(), [])

    def test_a_symlink_on_the_host_cannot_drift(self):
        self.m.ship("home/aaa/.local/bin/a3-core.py", "code")
        link = self.m.root / "home/aaa/.local/bin/a3-core.py"
        link.parent.mkdir(parents=True)
        link.symlink_to(self.m.root / "elsewhere")
        self.assertEqual(self.changes(), [])

    def test_a_path_with_spaces_survives(self):
        self.m.ship("home/aaa/.config/a3/a b.conf", "a")
        self.m.put("home/aaa/.config/a3/a b.conf", "edited")
        self.assertEqual(self.changes(), ["home/aaa/.config/a3/a b.conf"])

    def test_dpkg_sums_are_read_on_the_host(self):
        self.m.ship("etc/rtirq.conf", "newer in the repo")
        self.m.put("etc/rtirq.conf", "as installed")
        self.m.put("var/lib/dpkg/info/a3-core.md5sums", f"{md5('as installed')}  etc/rtirq.conf\n")
        self.assertEqual(self.changes(), [])

    def test_behind_is_judged_by_the_hosts_content(self):
        self.m.ship("home/aaa/.config/systemd/user/a3-core.service", "new")
        self.m.put("home/aaa/.config/systemd/user/a3-core.service", "old")
        found = self.found(lambda package_file: {mirror_check.meaning_hash(b"old")})
        self.assertEqual([kind for _, _, kind in found], [mirror_check.BEHIND])

    def test_take_copies_the_hosts_version_into_the_package(self):
        shipped = self.m.ship("etc/rtirq.conf", "a")
        self.m.put("etc/rtirq.conf", "edited on the rig")
        host = self.host()
        mirror_check.take(mirror_check.local_changes(self.m.pairs(), {}, machine=host),
                          machine=host)
        self.assertEqual(shipped.read_text(), "edited on the rig")

    def test_a_new_file_beside_shipped_ones_on_the_host_is_named(self):
        self.m.ship("home/aaa/.local/bin/a3-core.py", "x")
        self.m.put("home/aaa/.local/bin/a3-core.py", "x")
        self.m.put("home/aaa/.local/bin/a3-new-helper", "y")
        beside = mirror_check.new_beside(self.m.pairs(), self.m.root, machine=self.host())
        self.assertEqual([str(Path(p).relative_to(self.m.root)) for p in beside],
                         ["home/aaa/.local/bin/a3-new-helper"])

    def test_an_ssh_failure_is_an_error_not_a_clean_mirror(self):
        self.m.ship("etc/rtirq.conf", "a")
        with self.assertRaises(mirror_check.HostUnreadable):
            self.host(FakeSsh(fail="ssh: connect to host a3nuc1: No route to host"))

    def test_a_cut_off_answer_is_an_error(self):
        self.m.ship("etc/rtirq.conf", "a")
        with self.assertRaises(mirror_check.HostUnreadable):
            self.host(FakeSsh(answer="M\t0\n"))

    def test_an_unreadable_file_on_the_host_is_an_error(self):
        self.m.ship("etc/rtirq.conf", "a")
        live = self.m.put("etc/rtirq.conf", "secret")
        live.chmod(0)
        try:
            with self.assertRaises(mirror_check.HostUnreadable):
                self.host()
        finally:
            live.chmod(0o644)

    def test_main_stops_when_the_host_cannot_be_read(self):
        out = io.StringIO()
        with mock.patch("mirror_check.run_ssh", FakeSsh(fail="Connection refused")), \
                redirect_stdout(out):
            code = mirror_check.main(["--host", "a3nuc1"])
        self.assertEqual(code, 2)
        self.assertIn("a3nuc1", out.getvalue())
        self.assertNotIn("mirrors", out.getvalue())

    def test_main_without_host_reads_this_machine(self):
        with mock.patch("mirror_check.run_ssh") as ssh, redirect_stdout(io.StringIO()):
            mirror_check.main([])
        ssh.assert_not_called()
