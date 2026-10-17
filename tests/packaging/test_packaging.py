# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pins the Debian packaging that a3-system's installer/package.py reads:
packaging/stage lays out the bench binary, packaging/DEBIAN/control names it."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STAGE = ROOT / "packaging" / "stage"
CONTROL = ROOT / "packaging" / "DEBIAN" / "control"


# /tmp is a small tmpfs: scratch copies live next to the worktrees.
SCRATCH_ROOT = ROOT.parents[1]

CC_BY_ATTRIBUTION = (
    'HRIR data: Benjamin Bernsch\u00fctz, TH K\u00f6ln, "A Spherical Far Field HRIR/HRTF Compilation '
    'of the Neumann KU 100", Proceedings of the 40th Italian (AIA) Annual Conference on Acoustics and '
    'the 39th German Annual Conference on Acoustics (DAGA), Merano 2013; dataset '
    'https://zenodo.org/records/3928297, licensed CC BY 3.0 '
    '(https://creativecommons.org/licenses/by/3.0/). Binaural IRs rendered from it by IEM (MagLS); '
    'headphone EQs by Benjamin Bernsch\u00fctz.')


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
            build = Path(tmp, "build")
            (build / "bench").mkdir(parents=True)
            (build / "bench" / "a3-engine-bench").write_text("#!/bin/sh\n")
            stage = Path(tmp, "stage")
            subprocess.run([str(STAGE), "files", str(ROOT), str(build), str(stage)], check=True)
            installed = stage / "usr" / "bin" / "a3-engine-bench"
            self.assertTrue(installed.is_file())
            self.assertTrue(os.access(installed, os.X_OK))

    def test_the_shipped_copyright_carries_the_mit_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp, "build")
            (build / "bench").mkdir(parents=True)
            (build / "bench" / "a3-engine-bench").write_text("#!/bin/sh\n")
            stage = Path(tmp, "stage")
            subprocess.run([str(STAGE), "files", str(ROOT), str(build), str(stage)], check=True)
            copyright = (stage / "usr/share/doc/a3-engine-bench/copyright").read_text()
        self.assertIn("\nLicense: MIT\n", copyright)
        self.assertIn("Copyright (c) 2018 Chris Johnson", copyright)
        # the licence body, line for line (blank lines are " ." in this format)
        for line in (ROOT / "LICENSES" / "MIT.txt").read_text().splitlines():
            self.assertIn(" " + (line or "."), copyright.splitlines())

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
        copyright = (self.stage_files() / "usr/share/doc/a3-engine-bench/copyright").read_text()
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
        build = Path(tmp, "build")
        (build / "bench").mkdir(parents=True)
        (build / "bench" / "a3-engine-bench").write_text("#!/bin/sh\n")
        stage = Path(tmp, "stage")
        subprocess.run([str(STAGE), "files", str(ROOT), str(build), str(stage)], check=True)
        return stage

    def test_unknown_mode_is_refused(self):
        result = subprocess.run([str(STAGE), "nonsense"], capture_output=True)
        self.assertEqual(result.returncode, 2)


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
        self.assertEqual(fields["Package"], "a3-engine-bench")
        self.assertIn("${shlibs:Depends}", fields["Depends"])
        self.assertEqual(fields["Architecture"], "amd64")

    @unittest.skipUnless(shutil.which("dpkg-shlibdeps") and shutil.which("objdump")
                         and (ROOT / "build/bench/a3-engine-bench").is_file(),
                         "needs dpkg-shlibdeps and a built bench")
    def test_every_needed_library_is_covered_by_shlibs(self):
        # Depends comes from dpkg-shlibdeps, so it cannot miss a NEEDED library; this pins that
        # each one resolves to a package. The X11 libraries JUCE only dlopens for a window system
        # are not NEEDED: the headless bench never loads them.
        binary = ROOT / "build/bench/a3-engine-bench"
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
        for package in ("libjack-jackd2-0", "libfreetype6", "libfontconfig1"):
            self.assertIn(package, result.stdout)

    def test_maintainer_scripts_exist_and_are_executable(self):
        # installer/package.py refuses a package without all three.
        for name in ("postinst", "prerm", "postrm"):
            script = CONTROL.parent / name
            self.assertTrue(os.access(script, os.X_OK), name)

    def test_the_truth_file_comes_with_a_recommended_a3_core(self):
        self.assertIn("a3-core", control_fields().get("Recommends", ""))


if __name__ == "__main__":
    unittest.main()
