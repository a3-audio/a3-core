# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Core says its own version in /api/status (2026-10-30), so the Canvas on any
machine shows what the Core runs. The version is the package's: a3-system's
installer/package.py hands it to the recipe as A3_PACKAGE_VERSION, and the
app's recipe hands it to CMake as A3_CORE_VERSION (src/a3-core/CMakeLists.txt),
as the Canvas's recipe does with A3CANVAS_VERSION. A build no package made
says "dev"."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.paths import APP_RECIPE

APP_STAGE = APP_RECIPE / "stage"


class TheRecipeHandsTheVersionToCMake(unittest.TestCase):
    def configure_arguments(self, version):
        """The arguments of the recipe's first cmake call, with cmake and cc stubbed."""
        with tempfile.TemporaryDirectory() as scratch:
            scratch = Path(scratch)
            log = scratch / "cmake.log"
            bin_dir = scratch / "bin"
            bin_dir.mkdir()
            for tool in ("cmake", "cc"):
                stub = bin_dir / tool
                stub.write_text(f'#!/bin/sh\necho "$@" >> "{log}"\n')
                stub.chmod(0o755)
            env = {**os.environ, "PATH": f"{bin_dir}:{os.environ['PATH']}", "CC": str(bin_dir / "cc")}
            env.pop("A3_PACKAGE_VERSION", None)
            if version is not None:
                env["A3_PACKAGE_VERSION"] = version
            subprocess.run([str(APP_STAGE), "build", str(scratch / "src"), str(scratch / "build"),
                            str(scratch / "juce"), "2"], check=True, env=env,
                           stdout=subprocess.DEVNULL)
            return log.read_text().splitlines()[0].split()

    def test_the_packages_version(self):
        self.assertIn("-DA3_CORE_VERSION=03.0+999", self.configure_arguments("03.0+999"))

    def test_none_without_a_package(self):
        self.assertIn("-DA3_CORE_VERSION=", self.configure_arguments(None))


class TheLibraryTakesIt(unittest.TestCase):
    def test_only_the_version_file_sees_it(self):
        cmake = (APP_RECIPE.parents[1] / "src/a3-core/CMakeLists.txt").read_text()
        self.assertIn('set(A3_CORE_VERSION "" CACHE STRING', cmake)
        self.assertIn("set_source_files_properties(Version.cpp PROPERTIES COMPILE_DEFINITIONS", cmake)


if __name__ == "__main__":
    unittest.main()
