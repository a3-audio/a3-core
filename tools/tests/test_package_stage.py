# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pins the Debian packaging that a3-system's installer/package.py reads, two recipes since
2026-10-29: packaging/app/stage lays out the a3-core program, its user units, the default config
and the bench (package a3-core-app, packaging/app/DEBIAN); packaging/stage lays out the deployment
payload (platform-config/debian-x86_64/a3-core), and packaging/DEBIAN/control names the package
a3-core, which takes over from the old a3-engine package. See test_packaging_split for the line
between the two."""

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.paths import APP_DEBIAN, APP_RECIPE, DEBIAN, PACKAGE, ROOT

STAGE = ROOT / "packaging" / "stage"
APP_STAGE = APP_RECIPE / "stage"
CONTROL = DEBIAN / "control"
UNIT = ROOT / "packaging" / "a3-engine.service"
WINDOW_UNIT = ROOT / "packaging" / "a3-core-window.service"
# The program (Task 23); its engine role is what the unit runs. Staging is Task 28's.
APP = ROOT / "build" / "app" / "a3-core"
TRUTH = ROOT / "tests" / "engine" / "data" / "a3-osc.json"
SHIPPED_CONFIG = ROOT / "data" / "engine.json"
SHIPPED_SPATIAL = ROOT / "data" / "spatial"
COPYRIGHT = "usr/share/doc/a3-core-app/copyright"
DEPLOYMENT_COPYRIGHT = "usr/share/doc/a3-core/copyright"
#: Every licence dep5 names that /usr/share/common-licenses does not carry.
UNCOMMON_LICENCES = ("MIT", "CC-BY-3.0", "Apache-2.0", "CC-BY-SA-4.0")


# The staging tests copy the whole package; where /tmp is a small tmpfs,
# point A3_SCRATCH at a roomier directory.
SCRATCH_ROOT = os.environ.get("A3_SCRATCH") or tempfile.gettempdir()

CC_BY_ATTRIBUTION = (
    'HRIR data: Benjamin Bernsch\u00fctz, TH K\u00f6ln, "A Spherical Far Field HRIR/HRTF Compilation '
    'of the Neumann KU 100", Proceedings of the 40th Italian (AIA) Annual Conference on Acoustics and '
    'the 39th German Annual Conference on Acoustics (DAGA), Merano 2013; dataset '
    'https://zenodo.org/records/3928297, licensed CC BY 3.0 '
    '(https://creativecommons.org/licenses/by/3.0/). Binaural IRs rendered from it by IEM (MagLS); '
    'headphone EQs by Benjamin Bernsch\u00fctz.')


def stub_build(build):
    """What the app's `files` takes from a build tree: two executables and the realtime preload."""
    for relative in ("bench/a3-engine-bench", "app/a3-core", "rt-preload/libjack-rt-priority.so"):
        (build / relative).parent.mkdir(parents=True, exist_ok=True)
        (build / relative).write_text("#!/bin/sh\n")
    return build


def unit_lines():
    return [line.strip() for line in UNIT.read_text().splitlines()
            if line.strip() and not line.startswith("#")]


def staged_files(case, stage_script=APP_STAGE):
    tmp = tempfile.mkdtemp(dir=SCRATCH_ROOT)
    case.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
    build = stub_build(Path(tmp, "build"))
    stage = Path(tmp, "stage")
    subprocess.run([str(stage_script), "files", str(ROOT), str(build), str(stage)], check=True)
    return stage


def control_entries(field, control=CONTROL):
    return [entry.strip() for entry in control_fields(control).get(field, "").split(",") if entry.strip()]


def control_fields(control=CONTROL):
    fields = {}
    for line in control.read_text().splitlines():
        if line and not line.startswith(" ") and ":" in line:
            key, value = line.split(":", 1)
            fields[key] = value.strip()
    return fields


class BenchRulesTest(unittest.TestCase):
    def test_bench_never_connects_ports(self):
        for source in (ROOT / "bench").glob("*.cpp"):
            self.assertNotIn("jack_connect", source.read_text(), source.name)


class StageTest(unittest.TestCase):
    def test_files_installs_the_bench_executable(self):
        installed = self.stage_files() / "usr" / "bin" / "a3-engine-bench"
        self.assertTrue(installed.is_file())
        self.assertTrue(os.access(installed, os.X_OK))

    def test_the_shipped_copyright_carries_the_mit_text(self):
        copyright = (self.stage_files() / COPYRIGHT).read_text()
        self.assertIn("\nLicense: MIT\n", copyright)
        self.assertIn("Copyright (c) 2018 Chris Johnson", copyright)
        # the licence body, line for line (blank lines are " ." in this format)
        for line in (ROOT / "LICENSES" / "MIT.txt").read_text().splitlines():
            self.assertIn(" " + (line or "."), copyright.splitlines())

    def test_ships_the_app_the_unit_and_the_default_config(self):
        stage = self.stage_files()
        for path in ("usr/bin/a3-core", "usr/bin/a3-engine-bench"):
            self.assertTrue(os.access(stage / path, os.X_OK), path)
        self.assertEqual((stage / "usr/lib/systemd/user/a3-engine.service").read_bytes(), UNIT.read_bytes())
        self.assertEqual((stage / "usr/share/a3-engine/engine.json").read_bytes(),
                         (ROOT / "data" / "engine.json").read_bytes())
        # engine.json names its spatial directory relative to itself; that directory ships beside it.
        self.assertTrue((stage / "usr/share/a3-engine/spatial/main.zone.json").is_file())

    def test_the_stages_hold_program_and_payload(self):
        stage = self.stage_files()
        for path in ("usr/bin/a3-core", "usr/bin/a3-engine-bench",
                     "usr/lib/systemd/user/a3-engine.service",
                     "usr/lib/systemd/user/a3-core-window.service",
                     "usr/share/a3-engine/engine.json", "usr/share/a3/a3-osc.json",
                     "usr/lib/a3-core/a3-wait-for-the-screen"):
            self.assertTrue((stage / path).is_file(), path)
        deployment = staged_files(self, STAGE)
        self.assertTrue((deployment / "home/aaa/.local/bin/a3-wait-for-the-screen").is_file())
        # DEBIAN/ is installer/package.py's, from <recipe>/DEBIAN; a recipe lays out the payload only.
        self.assertFalse((stage / "DEBIAN").exists())
        self.assertFalse((deployment / "DEBIAN").exists())

    def test_the_payload_is_staged_as_it_is_in_git(self):
        staged = {"usr/share/a3/a3-osc.json": (self.stage_files(), "usr/share/a3/a3-osc.json"),
                  "home/aaa/.local/bin/a3-wait-for-the-screen":
                      (staged_files(self, STAGE), "home/aaa/.local/bin/a3-wait-for-the-screen")}
        for relative, (stage, target) in staged.items():
            with self.subTest(path=relative):
                self.assertEqual((stage / target).read_bytes(), (PACKAGE / relative).read_bytes())
                self.assertEqual(os.access(stage / target, os.X_OK),
                                 os.access(PACKAGE / relative, os.X_OK))

    def test_each_package_carries_the_copyright_with_every_uncommon_licence(self):
        for stage, relative in ((self.stage_files(), COPYRIGHT),
                                (staged_files(self, STAGE), DEPLOYMENT_COPYRIGHT)):
            copyright = (stage / relative).read_text()
            lines = copyright.splitlines()
            for name in UNCOMMON_LICENCES:
                with self.subTest(package=relative, license=name):
                    self.assertIn("\nLicense: " + name + "\n", copyright)
                    for line in (ROOT / "LICENSES" / (name + ".txt")).read_text().splitlines():
                        self.assertIn(" " + (line or "."), lines)
            self.assertFalse((stage / "usr/share/doc/a3-engine").exists())

    def test_ships_a3_core_and_no_a3_engine_binary(self):
        stage = self.stage_files()
        self.assertTrue(os.access(stage / "usr/bin/a3-core", os.X_OK))
        self.assertFalse((stage / "usr/bin/a3-engine").exists())
        self.assertTrue(os.access(stage / "usr/bin/a3-engine-bench", os.X_OK))

    def test_no_python_in_the_package(self):
        self.assertNotIn("python", control_fields()["Depends"].lower())
        self.assertNotIn("python", control_fields(APP_DEBIAN / "control")["Depends"].lower())
        for path in [*CONTROL.parent.iterdir(), *APP_DEBIAN.iterdir(), STAGE, APP_STAGE, UNIT]:
            text = path.read_text().lower()
            self.assertNotIn("python", text, path.name)
            self.assertNotIn("pip ", text, path.name)
        stages = (self.stage_files(), staged_files(self, STAGE))
        for shipped in (p for stage in stages for p in stage.rglob("*") if p.is_file()):
            self.assertNotEqual(shipped.suffix, ".py", shipped)
            if shipped.stat().st_size and "doc" not in shipped.parts:
                self.assertFalse(shipped.read_bytes().startswith(b"#!/usr/bin/env python"), shipped)

    def test_files_installs_the_spatial_data(self):
        # The package default for the bench, and REAPER's 6.1 booth for the parity run (--spatial).
        stage = self.stage_files()
        for name in ("spatial", "spatial-reaper-booth-6.1"):
            with self.subTest(directory=name):
                originals = sorted((ROOT / "data" / name).glob("*.json"))
                self.assertTrue(originals)
                target = stage / "usr/share/a3-engine" / name
                for original in originals:
                    self.assertEqual((target / original.name).read_bytes(),
                                     original.read_bytes(), original.name)
                self.assertEqual(sorted(p.name for p in target.iterdir()),
                                 [p.name for p in originals])

    def test_the_shipped_copyright_carries_cc_by_and_apache(self):
        copyright = (self.stage_files() / COPYRIGHT).read_text()
        lines = copyright.splitlines()
        for name, file_name in (("CC-BY-3.0", "CC-BY-3.0.txt"), ("Apache-2.0", "Apache-2.0.txt")):
            with self.subTest(license=name):
                self.assertIn("\nLicense: " + name + "\n", copyright)
                for line in (ROOT / "LICENSES" / file_name).read_text().splitlines():
                    self.assertIn(" " + (line or "."), lines)
        self.assertIn(CC_BY_ATTRIBUTION, copyright)

    def stage_files(self):
        return staged_files(self)

    def test_unknown_mode_is_refused(self):
        result = subprocess.run([str(STAGE), "nonsense"], capture_output=True)
        self.assertEqual(result.returncode, 2)


class UnitTest(unittest.TestCase):
    def test_the_unit_keeps_its_place_in_a3_main_and_restarts_on_failure(self):
        lines = unit_lines()
        for line in ("PartOf=a3-main.service",
                     "After=pipewire.service wireplumber.service",
                     "Restart=on-failure", "RestartSec=1",
                     "WantedBy=default.target"):
            self.assertIn(line, lines)
        # a3-main Wants this unit with others in one transaction; a Conflicts= between them drops a
        # start job before any condition runs, and then nothing may run.
        for line in lines:
            self.assertFalse(line.startswith("Conflicts="), line)
        # A broken open (a port taken) must stop visibly, not restart for ever.
        self.assertIn("StartLimitIntervalSec=60", lines)
        self.assertIn("StartLimitBurst=5", lines)

    def test_the_engine_unit_runs_the_engine_role_headless(self):
        lines = unit_lines()
        self.assertIn("ExecStart=/usr/bin/a3-core --role engine", lines)
        self.assertIn("ExecStartPre=/usr/bin/jack_wait -w", lines)
        self.assertIn("CPUAffinity=1 2 3", lines)
        for line in lines:
            for forbidden in ("DISPLAY", "a3-wait-for-the-screen", "ExecCondition",
                              "a3-engine-runs", "--service", "/home/"):
                self.assertNotIn(forbidden, line)
            self.assertFalse(line.endswith("/usr/bin/a3-engine") or "/usr/bin/a3-engine " in line, line)
        text = UNIT.read_text()
        self.assertNotIn("a3-audio-engine", text)
        self.assertNotIn("A3_ENGINE_BINARY", text)

    def test_the_unit_leaves_the_priorities_to_the_jack_library(self):
        # Raising the whole process put its GUI/OSC threads above JACK's (2026-10-06).
        lines = unit_lines()
        self.assertIn("Environment=LD_PRELOAD=/usr/lib/a3-core/libjack-rt-priority.so", lines)
        for line in lines:
            self.assertFalse(line.startswith("CPUScheduling"), line)
            self.assertNotIn("stemdeck@", line)


class WindowUnitTest(unittest.TestCase):
    """The window comes up at boot under a3-main. a3-main (a3-core's package) Wants it; this unit
    has no [Install], so nothing enables it on its own."""

    def lines(self):
        return [l for l in WINDOW_UNIT.read_text().splitlines() if l.strip() and not l.startswith("#")]

    def test_the_package_ships_the_window_unit(self):
        stage = staged_files(self)
        shipped = stage / "usr/lib/systemd/user/a3-core-window.service"
        self.assertEqual(shipped.read_bytes(), WINDOW_UNIT.read_bytes())
        self.assertFalse(os.access(shipped, os.X_OK))   # 644: a unit file, not a program

    def test_the_window_unit_attaches_to_core_and_engine_and_follows_a3_main(self):
        lines = self.lines()
        self.assertIn("Description=the a3-core window", lines)
        self.assertIn("PartOf=a3-main.service", lines)
        self.assertIn("After=a3-core.service a3-engine.service", lines)

    def test_the_window_unit_runs_the_window_role_after_the_screen_is_there(self):
        lines = self.lines()
        self.assertIn("Environment=DISPLAY=:0", lines)
        self.assertIn("ExecStartPre=/usr/lib/a3-core/a3-wait-for-the-screen", lines)
        self.assertIn("ExecStart=/usr/bin/a3-core", lines)   # no role: the window
        for line in lines:
            self.assertNotIn("--role", line)

    def test_a_clean_close_is_not_restarted_but_a_crash_is(self):
        # The window exits 0 on a close and on SIGTERM (Main.cpp WindowRunner::tick), so on-failure
        # leaves a closed window closed; a crash or a kill signal restarts it.
        lines = self.lines()
        self.assertIn("Restart=on-failure", lines)
        self.assertIn("RestartSec=2", lines)
        # The quit question can hold SIGTERM; this bounds `systemctl stop`.
        self.assertIn("TimeoutStopSec=10", lines)

    def test_the_window_unit_has_no_install_no_conflicts_and_no_home_path(self):
        # a3-main Wants every unit in one transaction; a Conflicts= drops a start job before any
        # condition runs (R35).
        text = WINDOW_UNIT.read_text()
        self.assertNotIn("[Install]", text)
        self.assertNotIn("Conflicts=", text)
        for line in self.lines():
            self.assertFalse(line.startswith("Conflicts="), line)
            self.assertNotIn("/home/", line)
            self.assertNotIn("WantedBy", line)


class SubmoduleTest(unittest.TestCase):
    def test_build_refuses_without_the_submodule(self):
        tmp = tempfile.mkdtemp(dir=SCRATCH_ROOT)
        try:
            copy = Path(tmp, "tree")
            shutil.copytree(ROOT, copy, ignore=shutil.ignore_patterns(
                "build*", ".git", "IEMPluginSuite", "__pycache__", ".superpowers"))
            result = subprocess.run(["cmake", "-S", str(copy), "-B", str(Path(tmp, "b"))],
                                    capture_output=True, text=True)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        self.assertNotEqual(result.returncode, 0)
        # CMake wraps long messages, so compare with the whitespace collapsed.
        output = " ".join((result.stdout + result.stderr).split())
        self.assertIn("git submodule update --init third_party/IEMPluginSuite", output)


class ControlTest(unittest.TestCase):
    def test_control_names_a3_core_with_section_and_priority(self):
        # reprepro files a stage-built package by its Section and Priority.
        fields = control_fields()
        self.assertEqual(fields["Package"], "a3-core")
        self.assertEqual(fields["Architecture"], "amd64")
        self.assertEqual(fields["Section"], "sound")
        self.assertEqual(fields["Priority"], "optional")

    def test_control_takes_over_from_a3_engine(self):
        # The program came in the a3-engine package; this one carries its files now (design U9).
        for field in ("Conflicts", "Replaces"):
            self.assertIn("a3-engine", control_entries(field), field)
        self.assertNotIn("a3-engine", [entry.split()[0] for entry in control_entries("Depends")])
        self.assertNotIn("a3-engine-bench", CONTROL.read_text())
        # The binaries are a3-core-app's, so are the libraries they need.
        self.assertIn("${shlibs:Depends}", control_entries("Depends", APP_DEBIAN / "control"))

    def test_control_keeps_the_analyzer_rule(self):
        self.assertIn("beat-analyzer (>= 03.0+64)", control_entries("Recommends"))
        self.assertIn("onboard", control_entries("Recommends"))
        self.assertIn("beat-analyzer (<< 03.0+64)", control_entries("Breaks"))

    @unittest.skipUnless(shutil.which("dpkg-shlibdeps") and shutil.which("objdump")
                         and (ROOT / "build/bench/a3-engine-bench").is_file()
                         and (ROOT / "build/app/a3-core").is_file(),
                         "needs dpkg-shlibdeps and a built bench and app")
    def test_every_needed_library_is_covered_by_shlibs(self):
        # Depends comes from dpkg-shlibdeps, so it cannot miss a NEEDED library; this pins that
        # each one resolves to a package, for both shipped executables. The X11 libraries JUCE
        # only dlopens for a window system are not NEEDED by either. The app also links ALSA.
        packages = {
            "build/bench/a3-engine-bench": ("libjack-jackd2-0", "libfreetype6", "libfontconfig1"),
            "build/app/a3-core": ("libjack-jackd2-0", "libfreetype6", "libfontconfig1",
                                  "libasound2"),
        }
        for relative, expected in packages.items():
            with self.subTest(binary=relative):
                result = self.shlibdeps(ROOT / relative)
                for package in expected:
                    self.assertIn(package, result.stdout)

    def shlibdeps(self, binary):
        needed = [line.split()[1] for line in subprocess.run(
            ["objdump", "-p", str(binary)], capture_output=True, text=True, check=True
        ).stdout.splitlines() if line.strip().startswith("NEEDED")]
        self.assertIn("libjack.so.0", needed)
        tmp = tempfile.mkdtemp(dir=SCRATCH_ROOT)
        try:
            Path(tmp, "debian").mkdir()
            Path(tmp, "debian/control").write_text("Source: s\n\nPackage: p\nArchitecture: any\n")
            result = subprocess.run(["dpkg-shlibdeps", "-O", "-e" + str(binary)],
                                    cwd=tmp, capture_output=True, text=True, check=True)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        self.assertNotIn("no dependency information", result.stderr)
        return result

    def test_maintainer_scripts_exist_and_are_executable(self):
        # installer/package.py refuses a package without all three.
        for debian in (DEBIAN, APP_DEBIAN):
            for name in ("postinst", "prerm", "postrm"):
                script = debian / name
                self.assertTrue(os.access(script, os.X_OK), script)

    def test_the_window_recommends_the_on_screen_keyboard_and_never_depends_on_it(self):
        # The touch screen needs onboard to type into the window's fields; a laptop with a keyboard does not.
        fields = control_fields()
        self.assertIn("onboard", [name.strip() for name in fields.get("Recommends", "").split(",")])
        self.assertNotIn("onboard", fields["Depends"])

    def test_maintainer_scripts(self):
        postinst = (DEBIAN / "postinst").read_text()
        self.assertIn(". /usr/share/debconf/confmodule", postinst)
        self.assertIn('\ninstall_shipped_config "$SHIPPED_CONFIG"', postinst)
        enable = postinst.index("systemctl --global enable a3-engine.service")
        # Enabled for every user before aaa's user manager is reloaded and a3-main enabled.
        self.assertLess(enable, postinst.index("\nrun_user_install\n"))
        for line in postinst.splitlines():
            self.assertNotRegex(line.split("#", 1)[0], r"systemctl .*restart .*a3-", line)
        prerm = (DEBIAN / "prerm").read_text()
        self.assertIn("systemctl --global disable a3-engine.service", prerm)
        self.assertRegex(prerm, r"(?m)^\s*remove\|deconfigure\)$")
        self.assertEqual(prerm.count("disable"), 1)
        mode = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-s", "packaging/DEBIAN/preinst"],
                              capture_output=True, text=True, check=True).stdout.split()[:1]
        self.assertEqual(mode, ["100755"])



@unittest.skipUnless(APP.is_file(), "needs the built program (build/app/a3-core)")
class CheckConfigBinaryTest(unittest.TestCase):
    """The built program's engine role: --check-config prints exactly one line and exits 0 (ok), 1 (<file>: <reason>) or 2 (bad flag)."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(dir=SCRATCH_ROOT))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        (self.tmp / "home").mkdir()

    def run_check(self, *words):
        # DISPLAY names a server that does not exist: the check must not need X.
        env = dict(os.environ, HOME=str(self.tmp / "home"), DISPLAY=":87")
        return subprocess.run([str(APP), "--role", "engine", "--check-config", "--truth", str(TRUTH), *words],
                              capture_output=True, text=True, env=env, timeout=60)

    def config_with_spatial(self, spatial):
        config = self.tmp / "engine.json"
        config.write_text(json.dumps({"spatial": str(spatial)}))
        return config

    def assert_one_line(self, result):
        self.assertEqual(len(result.stdout.splitlines()), 1, result.stdout)

    def test_the_shipped_config_is_ok(self):
        result = self.run_check("--config", str(SHIPPED_CONFIG))
        self.assertEqual((result.returncode, result.stdout), (0, "ok\n"), result.stderr)

    def test_broken_json_names_the_file(self):
        config = self.tmp / "broken.json"
        config.write_text('{ "engine": ')
        result = self.run_check("--config", str(config))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assert_one_line(result)
        self.assertTrue(result.stdout.startswith(str(config) + ": "), result.stdout)

    def test_a_config_iem_refuses_fails(self):
        spatial = self.tmp / "spatial"
        shutil.copytree(SHIPPED_SPATIAL, spatial)
        fx = spatial / "fx.parameters.json"
        parameters = json.loads(fx.read_text())
        parameters["noSuchParameter"] = 1.0
        fx.write_text(json.dumps(parameters))
        config = self.config_with_spatial(spatial)
        result = self.run_check("--config", str(config))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assert_one_line(result)
        self.assertTrue(result.stdout.startswith(str(config) + ": "), result.stdout)
        self.assertIn("fx.parameters.json", result.stdout)

    def test_an_unknown_flag_exits_2(self):
        result = self.run_check("--bogus")
        self.assertEqual(result.returncode, 2)

    def test_the_check_writes_nothing(self):
        config = self.config_with_spatial(SHIPPED_SPATIAL)
        result = self.run_check("--config", str(config))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(list((self.tmp / "home").iterdir()), [])


if __name__ == "__main__":
    unittest.main()
