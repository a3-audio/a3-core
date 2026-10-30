# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Two packages from one repository (decided 2026-10-29): a3-core-app is the
program -- binaries, user units, engine data, helpers -- and installs on any
machine, a laptop included; a3-core is the Core machine's deployment and
depends on the app at its own version. Everything that belongs to the rig
(the conflicts with network-manager and bluez, the RT kernel, files under
/home/aaa, the network) stays in a3-core."""

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN, ROOT

APP_RECIPE = ROOT / "packaging" / "app"
APP_DEBIAN = APP_RECIPE / "DEBIAN"
DEPLOYMENT_STAGE = ROOT / "packaging" / "stage"
APP_STAGE = APP_RECIPE / "stage"
PROGRAM_UNITS = tuple(ROOT / "packaging" / name for name in
                      ("a3-engine.service", "a3-core-window.service", "a3-core.service"))
#: What the brief names, a laptop must be able to keep all of it.
LAPTOP_NEEDS = ("network-manager", "wpasupplicant", "bluez", "modemmanager", "blueman")
#: The old a3-core's conffiles that the app now carries under new names.
RENAMED_CONFFILES = ("/etc/security/limits.d/audio.conf",
                     "/etc/ld.so.conf.d/pipewire-jack-x86_64-linux-gnu.conf")
VERSION_TOKEN = "${a3:Version}"
SCRATCH_ROOT = os.environ.get("A3_SCRATCH") or tempfile.gettempdir()


def multiarch():
    for command in (["dpkg-architecture", "-qDEB_HOST_MULTIARCH"], ["gcc", "-print-multiarch"]):
        if shutil.which(command[0]):
            found = subprocess.run(command, capture_output=True, text=True).stdout.strip()
            if found:
                return found
    raise unittest.SkipTest("no multiarch triplet on this machine")


def stub_build(build):
    """What `files` takes from a build tree, as empty stand-ins."""
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


def shipped_paths(stage):
    return {"/" + str(path.relative_to(stage)) for path in stage.rglob("*")
            if path.is_file() or path.is_symlink()}


def control_fields(control):
    fields = {}
    for line in control.read_text().splitlines():
        if line and not line.startswith(" ") and ":" in line:
            key, value = line.split(":", 1)
            fields[key] = value.strip()
    return fields


def entries(control, field):
    return [entry.strip() for entry in control_fields(control).get(field, "").split(",")
            if entry.strip()]


def conffiles(debian):
    """(shipped, remove-on-upgrade) as dpkg reads DEBIAN/conffiles."""
    path = debian / "conffiles"
    lines = [l.strip() for l in path.read_text().splitlines()] if path.is_file() else []
    shipped = [l for l in lines if l.startswith("/")]
    removed = [l.split()[-1] for l in lines if l.startswith("remove-on-upgrade ")]
    return shipped, removed


def code_lines(text):
    return [line.split("#", 1)[0] for line in text.splitlines()]


class TheAppIsTheProgram(unittest.TestCase):
    def test_the_app_ships_the_program(self):
        shipped = shipped_paths(staged(self, APP_STAGE))
        triplet = multiarch()
        for path in ("/usr/bin/a3-core", "/usr/bin/a3-engine-bench",
                     "/usr/share/a3-engine/engine.json", "/usr/share/a3-engine/spatial/main.zone.json",
                     "/usr/share/a3/a3-osc.json",
                     "/usr/lib/systemd/user/a3-engine.service",
                     "/usr/lib/systemd/user/a3-core-window.service",
                     "/usr/lib/systemd/user/a3-core.service",
                     "/usr/lib/systemd/user/a3-clock.service",
                     "/usr/lib/a3-core/libjack-rt-priority.so",
                     "/usr/lib/a3-core/a3-ptp-state",
                     "/usr/lib/a3-core/a3-wait-for-the-screen",
                     "/etc/security/limits.d/a3-audio.conf",
                     f"/etc/ld.so.conf.d/00-a3-pipewire-jack-{triplet}.conf",
                     "/usr/share/doc/a3-core-app/copyright"):
            self.assertIn(path, shipped)

    def test_the_app_ships_nothing_under_home(self):
        for path in shipped_paths(staged(self, APP_STAGE)):
            self.assertFalse(path.startswith("/home"), path)

    def test_the_helpers_are_executable(self):
        stage = staged(self, APP_STAGE)
        for relative in ("usr/bin/a3-core", "usr/bin/a3-engine-bench",
                         "usr/lib/a3-core/a3-wait-for-the-screen"):
            self.assertTrue(os.access(stage / relative, os.X_OK), relative)

    def test_the_loader_file_names_pipewire_jacks_directory_for_this_machine(self):
        triplet = multiarch()
        stage = staged(self, APP_STAGE)
        text = (stage / f"etc/ld.so.conf.d/00-a3-pipewire-jack-{triplet}.conf").read_text()
        lines = [l.strip() for l in text.splitlines() if l.strip() and not l.startswith("#")]
        self.assertEqual(lines, [f"/usr/lib/{triplet}/pipewire-0.3/jack/"])

    def test_the_app_conflicts_with_nothing_the_laptop_needs(self):
        control = APP_DEBIAN / "control"
        self.assertNotIn("Conflicts", control_fields(control))
        text = control.read_text()
        for package in LAPTOP_NEEDS:
            self.assertNotRegex(text, rf"(?<![\w-]){re.escape(package)}(?![\w-])", package)
        self.assertNotIn("linux-image-rt", text)

    def test_the_app_takes_over_from_the_old_deployment(self):
        control = APP_DEBIAN / "control"
        fields = control_fields(control)
        self.assertEqual(fields["Package"], "a3-core-app")
        self.assertEqual(entries(control, "Depends"),
                         ["${shlibs:Depends}", "pipewire", "pipewire-jack", "wireplumber",
                          "jack-example-tools"])
        for field in ("Breaks", "Replaces"):
            self.assertIn(f"a3-core (<< {VERSION_TOKEN})", entries(control, field), field)

    def test_the_apps_maintainer_scripts_name_no_home_and_no_user(self):
        for path in sorted(p for p in APP_DEBIAN.iterdir() if p.name != "control"):
            text = path.read_text()
            with self.subTest(script=path.name):
                self.assertNotIn("/home", text)
                self.assertNotRegex(text, r"\baaa\b")
                self.assertNotIn("sudo", text)

    def test_the_app_refreshes_the_loader_cache(self):
        self.assertRegex((APP_DEBIAN / "postinst").read_text(), r"(?m)^\s*ldconfig\b")
        self.assertRegex((APP_DEBIAN / "postrm").read_text(), r"(?m)^\s*ldconfig\b")

    def test_units_name_no_home_paths(self):
        for unit in PROGRAM_UNITS:
            text = unit.read_text()
            with self.subTest(unit=unit.name):
                self.assertNotIn("%h/.local", text)
                self.assertNotIn("/home/", text)

    def test_the_core_unit_runs_the_core_role_in_a3_main(self):
        lines = [l.strip() for l in (ROOT / "packaging/a3-core.service").read_text().splitlines()]
        self.assertIn("ExecStart=/usr/bin/a3-core --role core", lines)
        self.assertIn("PartOf=a3-main.service", lines)


class TheDeploymentIsTheMachine(unittest.TestCase):
    def test_the_deployment_depends_on_the_app_at_its_own_version(self):
        control = DEBIAN / "control"
        self.assertEqual(control_fields(control)["Package"], "a3-core")
        self.assertIn(f"a3-core-app (= {VERSION_TOKEN})", entries(control, "Depends"))
        # No binary left in it: installer/package.py refuses the token without one.
        self.assertNotIn("${shlibs:Depends}", control.read_text())

    def test_the_deployment_keeps_the_rig(self):
        control = DEBIAN / "control"
        for package in ("network-manager", "wpasupplicant", "bluez"):
            self.assertIn(package, entries(control, "Conflicts"))
        self.assertIn("linux-image-rt-amd64", entries(control, "Depends"))

    def test_no_file_is_in_both_packages(self):
        app = shipped_paths(staged(self, APP_STAGE))
        deployment = shipped_paths(staged(self, DEPLOYMENT_STAGE))
        self.assertTrue(app)
        self.assertTrue(deployment)
        self.assertEqual(app & deployment, set())

    def test_the_deployment_ships_no_program(self):
        # Its user units and drop-ins live beside the app's (every A3 unit in
        # one place, 2026-10-31); nothing else of it is under /usr.
        shipped = shipped_paths(staged(self, DEPLOYMENT_STAGE))
        for path in shipped:
            if path.startswith("/usr/lib/systemd/user/"):
                continue
            self.assertFalse(path.startswith(("/usr/bin/", "/usr/lib/", "/usr/share/a3")), path)

    def test_the_deployment_builds_nothing(self):
        # package.py runs `build` for every recipe; the deployment has nothing to compile.
        tmp = Path(tempfile.mkdtemp(dir=SCRATCH_ROOT))
        self.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
        subprocess.run([str(DEPLOYMENT_STAGE), "build", str(ROOT), str(tmp / "build"),
                        str(tmp / "no-juce"), "2"], check=True)
        self.assertFalse((tmp / "build").exists())

    def test_the_deployment_no_longer_compiles_the_preload(self):
        postinst = "\n".join(code_lines((DEBIAN / "postinst").read_text()))
        self.assertNotIn("gcc", postinst)
        self.assertNotIn("jack-rt-priority", postinst)
        self.assertNotRegex(postinst, r"(?m)^\s*ldconfig\b")


class ConffilesAreOwnedOnce(unittest.TestCase):
    def test_no_conffile_is_listed_in_both_packages(self):
        app, _ = conffiles(APP_DEBIAN)
        deployment, _ = conffiles(DEBIAN)
        self.assertEqual(set(app) & set(deployment), set())

    def test_every_listed_conffile_is_in_its_packages_stage(self):
        for debian, stage_script in ((APP_DEBIAN, APP_STAGE), (DEBIAN, DEPLOYMENT_STAGE)):
            listed, removed = conffiles(debian)
            shipped = shipped_paths(staged(self, stage_script))
            with self.subTest(package=debian.parent.name):
                for path in listed:
                    self.assertIn(path, shipped)
                # dpkg wants a remove-on-upgrade conffile absent from the package.
                for path in removed:
                    self.assertNotIn(path, shipped)

    def test_the_app_lists_its_limits_file(self):
        listed, _ = conffiles(APP_DEBIAN)
        self.assertIn("/etc/security/limits.d/a3-audio.conf", listed)

    def test_the_old_names_go_on_upgrade(self):
        listed, removed = conffiles(DEBIAN)
        for path in RENAMED_CONFFILES:
            self.assertNotIn(path, listed)
            self.assertIn(path, removed)


if __name__ == "__main__":
    unittest.main()
