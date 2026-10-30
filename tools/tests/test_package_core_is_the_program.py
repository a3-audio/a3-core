# SPDX-FileCopyrightText: 2026 Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""The cut: the a3-core program (/usr/bin/a3-core, built and shipped by this
package since the a3-engine repo moved in) is the Core, and no Python runs on
the rig.

The units switched by drop-ins first, while the deployment copied them into
~/.config; since 2026-10-31 every unit ships under /usr/lib/systemd/user and
names the program itself. REAPER, which stayed frozen beside the program until
2026-10-27, is gone from the package."""

import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN, DEPLOYMENT_UNITS, LOCAL, PACKAGE, ROOT
from tools.tests.test_package_installs_what_its_units_run import exec_lines

UNITS = DEPLOYMENT_UNITS
SYSTEM_UNITS = PACKAGE / "etc/systemd/system"
RECIPES = LOCAL / "share/a3-core/recipes"
PROGRAM = "/usr/bin/a3-core"

#: The first beat-analyzer that no longer meters.
ANALYZER_NO_VU = "03.0+64"

#: Frozen with REAPER during the cut, deleted with it on 2026-10-27: none of
#: these ships.
REAPER_FILES = (
    UNITS / "a3-reaper.service",
    UNITS / "a3-reaper.service.d/audio-engine.conf",
    LOCAL / "share/a3-core/config/REAPER",
    LOCAL / "share/a3-core/layout.json",
    LOCAL / "share/a3-core/curves-golden.json",
)


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args],
                          capture_output=True, check=True).stdout


def shipped(directory):
    """The files the package ships under `directory`: tracked (the package is
    a git archive) and present."""
    names = git("ls-files", "-z", "--", str(directory.relative_to(ROOT))).decode()
    return [ROOT / n for n in names.split("\0") if n and (ROOT / n).is_file()]


def control_field(name):
    text = (DEBIAN / "control").read_text()
    return re.search(rf"^{name}:(.*)$", text, re.MULTILINE).group(1)


def entries(field):
    return [e.strip() for e in control_field(field).split(",")]


def code(path):
    """A shell script without its comments."""
    return "\n".join(l.split("#", 1)[0] for l in path.read_text().splitlines())


def run(command, env_extra=None, **kwargs):
    env = dict(os.environ, **(env_extra or {}))
    return subprocess.run(command, capture_output=True, text=True, env=env, **kwargs)


class TheUnitsRunTheProgram(unittest.TestCase):
    def test_the_core_unit_runs_the_program(self):
        self.assertEqual([f"{PROGRAM} --role core"],
                         exec_lines(ROOT / "packaging/a3-core.service")["ExecStart"])

    def test_the_bar_unit_runs_the_program(self):
        self.assertEqual([f"{PROGRAM} --role i3-bar"],
                         exec_lines(UNITS / "a3-bar-per-workspace.service")["ExecStart"])


class NoPythonAtRuntime(unittest.TestCase):
    def test_no_python_at_runtime(self):
        for path in shipped(LOCAL):
            with self.subTest(path=path.relative_to(PACKAGE)):
                self.assertNotEqual(".py", path.suffix)
                with path.open("rb") as file:
                    first = file.readline()
                if first.startswith(b"#!"):
                    self.assertNotIn(b"python", first)

        for unit in [*UNITS.glob("*.service"), *SYSTEM_UNITS.glob("*.service")]:
            with self.subTest(unit=unit.name):
                self.assertNotIn("python", unit.read_text().lower())
        for drop_in in [*UNITS.glob("*.d/*.conf"), *SYSTEM_UNITS.glob("*.d/*.conf")]:
            with self.subTest(drop_in=drop_in.relative_to(PACKAGE)):
                self.assertNotIn("python", drop_in.read_text().lower())
        for script in [*shipped(RECIPES), DEBIAN / "postinst", DEBIAN / "preinst",
                       DEBIAN / "postrm"]:
            with self.subTest(script=script.name):
                text = script.read_text().lower()
                self.assertNotIn("python", text)
                self.assertNotIn("venv", text)
                self.assertNotIn("requirements.txt", text)

        self.assertEqual([], [e for e in entries("Depends") if e.startswith("python3")])


class ThePostinstRendersThroughTheProgram(unittest.TestCase):
    def test_postinst_renders_through_the_program(self):
        text = (DEBIAN / "postinst").read_text()
        self.assertNotIn("a3-osc-render", text)
        self.assertRegex(text, r'(?m)^\s*sudo -u "\$APPUSER" -H /usr/bin/a3-core --render-truth network-file')
        self.assertRegex(
            text,
            r'(?m)^\s*A3_NETWORK="\$\{USER_HOME\}/\.config/a3/network\.json" /usr/bin/a3-core --render-truth network$')
        self.assertRegex(text, r'(?m)^sudo -u "\$APPUSER" -H /usr/bin/a3-core --render-truth user')
        self.assertNotRegex(text, r"(?m)^[^#]*\bpip\b")


class ReaperIsGone(unittest.TestCase):
    def test_none_of_reapers_files_ships(self):
        for path in REAPER_FILES:
            self.assertFalse(path.exists(), path)


class TheProgramIsInstalled(unittest.TestCase):
    def test_the_program_is_in_this_package(self):
        # No second package to depend on: packaging/stage builds the program into this one.
        self.assertNotIn("a3-engine", [e.split()[0] for e in entries("Depends")])
        # It takes over the old a3-engine package's files, and removes it on the upgrade.
        self.assertIn("a3-engine", entries("Conflicts"))
        self.assertIn("a3-engine", entries("Replaces"))
        self.assertTrue((ROOT / "packaging/a3-engine.service").is_file())
        self.assertIn(f"beat-analyzer (>= {ANALYZER_NO_VU})", entries("Recommends"))
        self.assertNotIn("beat-analyzer", control_field("Depends"))


class TheScreencastRunsTheProgram(unittest.TestCase):
    SCRIPT = LOCAL / "bin/a3-screencast"

    def test_screencast_runs_the_programs_fanout(self):
        text = code(self.SCRIPT)
        self.assertIn('exec nice -n 19 taskset -c "$CPUS" /usr/bin/a3-core --role screencast-fanout'
                      ' --port "$A3_SCREENCAST_PORT" -- \\', text)
        self.assertLess(text.index("--role screencast-fanout"), text.index("ffmpeg -hide_banner"))
        for gone in ("python", "FANOUT"):
            self.assertNotIn(gone, self.SCRIPT.read_text())


class TheViewerAsksTheProgram(unittest.TestCase):
    RECIPE = RECIPES / "a3vnc.sh"

    def test_vnc_recipe_asks_the_program(self):
        text = code(self.RECIPE)
        self.assertIn("/usr/bin/a3-core", text)
        self.assertIn("--truth host core", text)
        self.assertIn("--truth port x11vnc vnc", text)
        with tempfile.TemporaryDirectory() as tmp:
            fake_core = Path(tmp) / "a3-core"
            fake_core.write_text('#!/bin/sh\ncase "$*" in\n'
                                 '  "--truth host core") echo 10.1.2.3 ;;\n'
                                 '  "--truth port x11vnc vnc") echo 5999 ;;\n'
                                 '  *) exit 1 ;;\nesac\n')
            viewer = Path(tmp) / "vncviewer"
            viewer.write_text('#!/bin/sh\necho "$@"\n')
            for fake in (fake_core, viewer):
                fake.chmod(0o755)
            done = run(["bash", str(self.RECIPE)],
                       {"A3_CORE": str(fake_core), "PATH": f"{tmp}:{os.environ['PATH']}"},
                       check=True)
        self.assertEqual("QualityLevel 2 10.1.2.3::5999", done.stdout.strip())

    def test_a_question_the_program_cannot_answer_starts_no_viewer(self):
        with tempfile.TemporaryDirectory() as tmp:
            fake_core = Path(tmp) / "a3-core"
            fake_core.write_text("#!/bin/sh\necho \"a3-core: no host 'core'\" >&2\nexit 1\n")
            viewer = Path(tmp) / "vncviewer"
            viewer.write_text('#!/bin/sh\necho "$@"\n')
            for fake in (fake_core, viewer):
                fake.chmod(0o755)
            done = run(["bash", str(self.RECIPE)],
                       {"A3_CORE": str(fake_core), "PATH": f"{tmp}:{os.environ['PATH']}"})
        self.assertNotEqual(0, done.returncode)
        self.assertEqual("", done.stdout)


if __name__ == "__main__":
    unittest.main()
