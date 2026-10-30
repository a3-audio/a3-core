# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Every A3 user unit in one place (decided 2026-10-31).

The program's units came from a3-core-app under /usr/lib/systemd/user, while
the deployment copied its own -- a3-main, a3-aes67, a3-screencast, the bar and
a second a3-core.service -- into ~/.config/systemd/user, where they hid the
packaged ones: the rig's a3-core.service still said "python osc server", and
only a drop-in made it run the program. Now both packages ship their units
under /usr/lib/systemd/user, each unit once, and ~/.config holds only the
machine's own drop-ins.

An installed machine still has the old copies. The postinst moves a copy aside
to <name>.before-one-place -- never deletes it -- but only one the deployment
itself put there: its checksum is one of a version the package once shipped
(git history, tools/units_before_one_place.py). A file changed by hand stays
where it is and is named.
"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN, DEPLOYMENT_UNITS, PACKAGE, ROOT

POSTINST = DEBIAN / "postinst"
DEPLOYMENT_STAGE = ROOT / "packaging" / "stage"
APP_STAGE = ROOT / "packaging" / "app" / "stage"
SCRATCH_ROOT = os.environ.get("A3_SCRATCH") or tempfile.gettempdir()
GENERATOR = ROOT / "tools" / "units_before_one_place.py"

APP_UNITS = {"a3-engine.service", "a3-core-window.service", "a3-core.service",
             "a3-clock.service"}
DEPLOYMENT_UNIT_NAMES = {"a3-main.service", "a3-aes67.service", "a3-screencast.service",
                         "a3-bar-per-workspace.service"}
DEPLOYMENT_DROP_INS = {"beat-analyzer.service.d/a3-core.conf",
                       "stemdeck.service.d/a3-core.conf",
                       "stemdeck.service.d/two-deck-off.conf"}
PACKAGED = "/usr/lib/systemd/user"


def postinst_function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(),
                     re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


def stub_build(build):
    for relative in ("app/a3-core", "bench/a3-engine-bench", "rt-preload/libjack-rt-priority.so"):
        (build / relative).parent.mkdir(parents=True, exist_ok=True)
        (build / relative).write_text("stand-in\n")
    return build


def staged(case, stage_script):
    tmp = tempfile.mkdtemp(dir=SCRATCH_ROOT)
    case.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
    build = stub_build(Path(tmp, "build"))
    stage = Path(tmp, "stage")
    subprocess.run([str(stage_script), "files", str(ROOT), str(build), str(stage)], check=True)
    return stage


def user_units_in(stage):
    """Unit files and drop-ins a stage lays out, relative to the user unit directory."""
    directory = stage / PACKAGED.lstrip("/")
    if not directory.is_dir():
        return set()
    return {str(p.relative_to(directory)) for p in directory.rglob("*") if p.is_file()}


def known_versions():
    """{rel: {sha256, ...}} as the postinst knows them."""
    found = {}
    script = f"{postinst_function('units_before_one_place')}\nunits_before_one_place\n"
    out = subprocess.run(["sh", "-c", script], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        digest, rel = line.split("  ", 1)
        found.setdefault(rel, set()).add(digest)
    return found


class EveryUnitShipsOnce(unittest.TestCase):
    def test_the_app_ships_the_programs_units(self):
        self.assertEqual(user_units_in(staged(self, APP_STAGE)), APP_UNITS)

    def test_the_deployment_ships_its_units_and_drop_ins_under_usr_lib(self):
        self.assertEqual(user_units_in(staged(self, DEPLOYMENT_STAGE)),
                         DEPLOYMENT_UNIT_NAMES | DEPLOYMENT_DROP_INS)

    def test_no_unit_is_in_both_packages(self):
        app = user_units_in(staged(self, APP_STAGE))
        deployment = user_units_in(staged(self, DEPLOYMENT_STAGE))
        names = [Path(rel).name for rel in app | deployment if rel.endswith(".service")]
        self.assertEqual(len(names), len(set(names)))

    def test_nothing_lands_in_a_users_systemd_directory(self):
        for path in PACKAGE.rglob("*"):
            relative = str(path.relative_to(PACKAGE))
            with self.subTest(path=relative):
                self.assertFalse(relative.startswith("home/") and "/systemd/" in relative)

    def test_no_unit_source_is_kept_in_two_places(self):
        program = {p.name for p in (ROOT / "packaging").glob("*.service")}
        deployment = {p.name for p in DEPLOYMENT_UNITS.glob("*.service")}
        self.assertEqual(program, APP_UNITS)
        self.assertEqual(program & deployment, set())

    def test_the_bar_runs_the_program_without_a_drop_in(self):
        unit = (DEPLOYMENT_UNITS / "a3-bar-per-workspace.service").read_text()
        self.assertIn("ExecStart=/usr/bin/a3-core --role i3-bar", unit.splitlines())
        self.assertFalse((DEPLOYMENT_UNITS / "a3-bar-per-workspace.service.d").exists())

    def test_packaged_units_name_no_users_home(self):
        for unit in DEPLOYMENT_UNITS.rglob("*"):
            if unit.is_file():
                with self.subTest(unit=unit.name):
                    self.assertNotIn("/home/", unit.read_text())


class TheEnableStatesStay(unittest.TestCase):
    """a3-main and the bar were enabled by the links the deployment put into
    ~/.config; the screencast is each machine's choice and a3-core is pulled in
    by a3-main, never enabled itself."""

    def enabled(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "log"
            script = (f'user_systemctl() {{ echo "$*" >> {log}; }}\n'
                      f'{postinst_function("enable_the_rig")}\nenable_the_rig\n')
            subprocess.run(["sh", "-c", f"set -e\n{script}"], check=True)
            return log.read_text().splitlines()

    def test_the_rig_and_the_bar_are_enabled(self):
        self.assertIn("enable a3-main.service", self.enabled())
        self.assertIn("enable a3-bar-per-workspace.service", self.enabled())

    def test_the_screencast_and_the_core_are_not(self):
        lines = " ".join(self.enabled())
        self.assertNotIn("a3-screencast", lines)
        self.assertNotIn("a3-core.service", lines)

    def test_no_dead_wants_links_ship(self):
        self.assertEqual(sorted(PACKAGE.rglob("default.target.wants")), [])


class TheOldCopiesMoveAside(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.units = Path(self.tmp.name) / "config/systemd/user"
        (self.units / "default.target.wants").mkdir(parents=True)
        self.versions = known_versions()

    def tearDown(self):
        self.tmp.cleanup()

    def old_text(self, rel):
        """A version the deployment once shipped, from git history."""
        found = subprocess.run(
            [str(GENERATOR), "--text", rel], capture_output=True, text=True, cwd=ROOT)
        if found.returncode != 0:
            self.skipTest("no git history here")
        return found.stdout

    def put(self, rel, text):
        path = self.units / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def enable(self, name):
        (self.units / "default.target.wants" / name).symlink_to(self.units / name)

    def move(self):
        script = (f"set -e\n{postinst_function('units_before_one_place')}\n"
                  f"{postinst_function('repoint_wants_links')}\n"
                  f"{postinst_function('move_aside_unit_copies')}\n"
                  'move_aside_unit_copies "$1"\necho went-on\n')
        done = subprocess.run(["sh", "-c", script, "sh", str(self.units)],
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("went-on", done.stdout)
        return done

    def test_a_deployment_copy_goes_aside_and_its_link_follows_the_package(self):
        text = self.old_text("a3-main.service")
        self.put("a3-main.service", text)
        self.enable("a3-main.service")
        done = self.move()
        self.assertFalse((self.units / "a3-main.service").exists())
        self.assertEqual((self.units / "a3-main.service.before-one-place").read_text(), text)
        link = self.units / "default.target.wants/a3-main.service"
        self.assertEqual(os.readlink(link), f"{PACKAGED}/a3-main.service")
        self.assertIn("a3-main.service", done.stdout)

    def test_a_link_by_the_home_path_follows_the_package_too(self):
        self.put("a3-bar-per-workspace.service", self.old_text("a3-bar-per-workspace.service"))
        link = self.units / "default.target.wants/a3-bar-per-workspace.service"
        link.symlink_to("/home/aaa/.config/systemd/user/a3-bar-per-workspace.service")
        self.move()
        self.assertEqual(os.readlink(link), f"{PACKAGED}/a3-bar-per-workspace.service")

    def test_a_link_to_someone_elses_unit_stays(self):
        link = self.units / "default.target.wants/a3-main.service"
        link.symlink_to("/opt/elsewhere/a3-main.service")
        self.put("a3-main.service", self.old_text("a3-main.service"))
        self.move()
        self.assertEqual(os.readlink(link), "/opt/elsewhere/a3-main.service")

    def test_a_deployment_drop_in_goes_aside(self):
        text = self.old_text("a3-core.service.d/program.conf")
        self.put("a3-core.service.d/program.conf", text)
        self.move()
        self.assertFalse((self.units / "a3-core.service.d/program.conf").exists())
        self.assertTrue((self.units / "a3-core.service.d/program.conf.before-one-place").is_file())

    def test_a_hand_changed_copy_stays_and_is_named(self):
        text = self.old_text("a3-screencast.service") + "# changed here\n"
        self.put("a3-screencast.service", text)
        self.enable("a3-screencast.service")
        done = self.move()
        self.assertEqual((self.units / "a3-screencast.service").read_text(), text)
        self.assertFalse((self.units / "a3-screencast.service.before-one-place").exists())
        self.assertEqual(os.readlink(self.units / "default.target.wants/a3-screencast.service"),
                         str(self.units / "a3-screencast.service"))
        self.assertIn("kept the hand-changed ~/.config/systemd/user/a3-screencast.service",
                      done.stdout)

    def test_a_machines_own_drop_in_and_a_mask_are_left_alone(self):
        own = self.put("a3-core.service.d/override.conf", "[Service]\nNice=5\n")
        (self.units / "a3-motion.service").symlink_to("/dev/null")
        done = self.move()
        self.assertEqual(own.read_text(), "[Service]\nNice=5\n")
        self.assertEqual(os.readlink(self.units / "a3-motion.service"), "/dev/null")
        self.assertNotIn("override.conf", done.stdout)

    def test_an_earlier_copy_aside_is_never_overwritten(self):
        text = self.old_text("a3-aes67.service")
        self.put("a3-aes67.service", text)
        self.put("a3-aes67.service.before-one-place", "older\n")
        done = self.move()
        self.assertEqual((self.units / "a3-aes67.service.before-one-place").read_text(), "older\n")
        self.assertEqual((self.units / "a3-aes67.service").read_text(), text)
        self.assertIn("a3-aes67.service", done.stderr)

    def test_nothing_there_is_nothing_to_do(self):
        done = self.move()
        self.assertEqual(done.stdout.strip(), "went-on")

    def test_it_runs_before_the_rig_is_enabled(self):
        text = POSTINST.read_text()
        self.assertLess(text.index('move_aside_unit_copies "${USER_HOME}'),
                        text.index("\nrun_user_install\n"))


class TheKnownVersionsAreTheHistorys(unittest.TestCase):
    def test_the_postinst_knows_every_version_git_has(self):
        found = subprocess.run([str(GENERATOR)], capture_output=True, text=True, cwd=ROOT)
        if found.returncode != 0:
            self.skipTest("no git history here")
        from_git = {}
        for line in found.stdout.splitlines():
            digest, rel = line.split("  ", 1)
            from_git.setdefault(rel, set()).add(digest)
        self.assertEqual(known_versions(), from_git)

    def test_it_covers_every_unit_and_drop_in_the_deployment_copied(self):
        self.assertEqual(set(known_versions()),
                         DEPLOYMENT_UNIT_NAMES | DEPLOYMENT_DROP_INS
                         | {"a3-core.service", "a3-core.service.d/cpu.conf",
                            "a3-core.service.d/program.conf",
                            "a3-bar-per-workspace.service.d/program.conf"})

    def test_the_rigs_copies_of_2026_10_31_are_known(self):
        versions = known_versions()
        for rel, digest in A3NUC1_COPIES.items():
            with self.subTest(copy=rel):
                self.assertIn(digest, versions[rel])


#: sha256 of a3nuc1's ~/.config/systemd/user copies, read on 2026-10-31: all
#: of them as the deployment put them there, so all of them go aside.
A3NUC1_COPIES = {
    "a3-aes67.service": "34c4fbffc8b4646f668fc7897aead769e46a094e5b64c2696bb75cb48bceb9ac",
    "a3-bar-per-workspace.service":
        "fa0a1b71e07f740c16b13803cdd60e22c8234f2c994cb369d3e27142a2331cd0",
    "a3-bar-per-workspace.service.d/program.conf":
        "13f1e531d5b4ba56272651d7fa36ba52be87ed77a1e04d8588856a704e02e519",
    "a3-core.service": "5d87b50276b0d6f36a3695eeaa540bb7fe9916dda5f1cc0e9bdd583f808e3914",
    "a3-core.service.d/cpu.conf":
        "6c47f85f480d49e461adcb563a4fe04e7f756e3aed9461eac94c186bdf9fe79f",
    "a3-core.service.d/program.conf":
        "9bf7be9edc5d220f9d786b67f0a8f65641b8c01331a6b50be2e3fe2c16fcb791",
    "a3-main.service": "971c3fc7614d07b2bdf9c39bd7feebef92d3fa80ffee261c436d5f78d0ac8c5e",
    "a3-screencast.service": "b87a766aa414ec8caeec3b4718656053c7fc31143ed92c6ec2652f3d73a3abe6",
    "beat-analyzer.service.d/a3-core.conf":
        "9e6e57a53674ad36ab304466efed5c9eefca4043b78228d6ca612017163c414f",
    "stemdeck.service.d/a3-core.conf":
        "298adbe6df0c49e3a37944552e990fa4d8cb6abf6e2caceb5a15005780fef36c",
    "stemdeck.service.d/two-deck-off.conf":
        "7eaae796ccdc3507b4fa5d569548004dbd3bbb2562f849e0fc88afd5903a7119",
}


class TheCoreScriptDropInRetires(unittest.TestCase):
    """a3nuc1's a3-core.service.d/override.conf starts ~/.local/bin/a3-core.py,
    which is gone; the deployment's program.conf sorted after it and won. With
    program.conf aside, the override would start a file that is not there."""

    def test_a_drop_in_that_starts_the_python_core_goes_to_the_backup(self):
        with tempfile.TemporaryDirectory() as tmp:
            config, backup = Path(tmp, "config"), Path(tmp, "backup")
            drop_ins = config / "systemd/user/a3-core.service.d"
            drop_ins.mkdir(parents=True)
            old = "[Service]\nExecStart=\nExecStart=/home/aaa/.venv/bin/python3 -u /home/aaa/.local/bin/a3-core.py\n"
            (drop_ins / "override.conf").write_text(old)
            (drop_ins / "nice.conf").write_text("[Service]\nNice=5\n")
            script = (f"set -e\n{postinst_function('retire_core_script_drop_ins')}\n"
                      'retire_core_script_drop_ins "$1" "$2"\necho went-on\n')
            done = subprocess.run(["sh", "-c", script, "sh", str(config), str(backup)],
                                  capture_output=True, text=True)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertFalse((drop_ins / "override.conf").exists())
            self.assertEqual(
                (backup / "systemd/user/a3-core.service.d/override.conf").read_text(), old)
            self.assertTrue((drop_ins / "nice.conf").is_file())
            self.assertIn("override.conf", done.stdout)


if __name__ == "__main__":
    unittest.main()
