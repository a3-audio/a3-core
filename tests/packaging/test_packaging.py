# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pins the Debian packaging that a3-system's installer/package.py reads:
packaging/stage lays out the a3-core program, its user unit, the default config and the bench;
packaging/DEBIAN/control names the package a3-engine."""

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STAGE = ROOT / "packaging" / "stage"
CONTROL = ROOT / "packaging" / "DEBIAN" / "control"
UNIT = ROOT / "packaging" / "a3-engine.service"
# The program (Task 23); its engine role is what the unit runs. Staging is Task 28's.
APP = ROOT / "build" / "app" / "a3-core"
TRUTH = ROOT / "tests" / "engine" / "data" / "a3-osc.json"
SHIPPED_CONFIG = ROOT / "data" / "engine.json"
SHIPPED_SPATIAL = ROOT / "data" / "spatial"


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
    """The two executables `files` takes from a build tree."""
    for relative in ("bench/a3-engine-bench", "app/a3-core"):
        (build / relative).parent.mkdir(parents=True, exist_ok=True)
        (build / relative).write_text("#!/bin/sh\n")
    return build


def unit_lines():
    return [line.strip() for line in UNIT.read_text().splitlines()
            if line.strip() and not line.startswith("#")]


def control_fields():
    fields = {}
    for line in CONTROL.read_text().splitlines():
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
        with tempfile.TemporaryDirectory() as tmp:
            build = stub_build(Path(tmp, "build"))
            stage = Path(tmp, "stage")
            subprocess.run([str(STAGE), "files", str(ROOT), str(build), str(stage)], check=True)
            installed = stage / "usr" / "bin" / "a3-engine-bench"
            self.assertTrue(installed.is_file())
            self.assertTrue(os.access(installed, os.X_OK))

    def test_the_shipped_copyright_carries_the_mit_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = stub_build(Path(tmp, "build"))
            stage = Path(tmp, "stage")
            subprocess.run([str(STAGE), "files", str(ROOT), str(build), str(stage)], check=True)
            copyright = (stage / "usr/share/doc/a3-engine/copyright").read_text()
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

    def test_the_package_writes_nothing_under_home(self):
        stage = self.stage_files()
        shipped = [p.relative_to(stage) for p in stage.rglob("*") if p.is_file()]
        self.assertTrue(shipped)
        for path in shipped:
            self.assertNotEqual(path.parts[0], "home", path)
        for name in ("postinst", "prerm", "postrm"):
            script = (CONTROL.parent / name).read_text()
            for forbidden in ("$HOME", "~", "/home/", "mkdir", "useradd"):
                self.assertNotIn(forbidden, script, name + " " + forbidden)

    def test_the_maintainer_scripts_enable_and_disable_the_user_unit(self):
        postinst = (CONTROL.parent / "postinst").read_text()
        self.assertIn("systemctl --global enable a3-engine.service", postinst)
        self.assertIn("daemon-reload", postinst)
        self.assertNotIn("restart", postinst.replace("no restart", ""))
        self.assertIn("systemctl --global disable a3-engine.service", (CONTROL.parent / "prerm").read_text())

    def test_ships_a3_core_and_no_a3_engine_binary(self):
        stage = self.stage_files()
        self.assertTrue(os.access(stage / "usr/bin/a3-core", os.X_OK))
        self.assertFalse((stage / "usr/bin/a3-engine").exists())
        self.assertTrue(os.access(stage / "usr/bin/a3-engine-bench", os.X_OK))

    def test_no_python_in_the_package(self):
        self.assertNotIn("python", control_fields()["Depends"].lower())
        for path in [*CONTROL.parent.iterdir(), STAGE, UNIT]:
            text = path.read_text().lower()
            self.assertNotIn("python", text, path.name)
            self.assertNotIn("pip ", text, path.name)
        stage = self.stage_files()
        for shipped in (p for p in stage.rglob("*") if p.is_file()):
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
        copyright = (self.stage_files() / "usr/share/doc/a3-engine/copyright").read_text()
        lines = copyright.splitlines()
        for name, file_name in (("CC-BY-3.0", "CC-BY-3.0.txt"), ("Apache-2.0", "Apache-2.0.txt")):
            with self.subTest(license=name):
                self.assertIn("\nLicense: " + name + "\n", copyright)
                for line in (ROOT / "LICENSES" / file_name).read_text().splitlines():
                    self.assertIn(" " + (line or "."), lines)
        self.assertIn(CC_BY_ATTRIBUTION, copyright)

    def stage_files(self):
        tmp = tempfile.mkdtemp(dir=SCRATCH_ROOT)
        self.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
        build = stub_build(Path(tmp, "build"))
        stage = Path(tmp, "stage")
        subprocess.run([str(STAGE), "files", str(ROOT), str(build), str(stage)], check=True)
        return stage

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
        # a3-main Wants a3-reaper.service and this unit in one transaction; a Conflicts= between them
        # drops a start job before any condition runs, and then nothing may run. REAPER stays off
        # through a3-core's own drop-in (ExecCondition a3-engine-runs --reaper), not through this unit.
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
        # Raising the whole process put its GUI/OSC threads above JACK's (a3-reaper.service says why).
        lines = unit_lines()
        self.assertIn("Environment=LD_PRELOAD=%h/.local/lib/a3/libjack-rt-priority.so", lines)
        for line in lines:
            self.assertFalse(line.startswith("CPUScheduling"), line)
            self.assertNotIn("stemdeck@", line)


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
    def test_names_the_package_and_takes_libraries_from_shlibs(self):
        fields = control_fields()
        self.assertEqual(fields["Package"], "a3-engine")
        for field in ("Replaces", "Conflicts", "Provides"):
            self.assertIn("a3-engine-bench", fields[field], field)
        self.assertIn("${shlibs:Depends}", fields["Depends"])
        self.assertEqual(fields["Architecture"], "amd64")

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
        for name in ("postinst", "prerm", "postrm"):
            script = CONTROL.parent / name
            self.assertTrue(os.access(script, os.X_OK), name)

    def test_the_program_breaks_the_python_core(self):
        self.assertRegex(control_fields()["Breaks"], r"a3-core \(<< [0-9][^)]*\)")

    def test_the_program_breaks_a_metering_analyzer(self):
        self.assertRegex(control_fields()["Breaks"], r"beat-analyzer \(<< [0-9][^)]*\)")

    def test_the_truth_file_comes_with_a_recommended_a3_core(self):
        self.assertIn("a3-core", control_fields().get("Recommends", ""))
        self.assertIn("pipewire-jack", control_fields().get("Recommends", ""))



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
