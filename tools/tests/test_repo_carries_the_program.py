# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""a3-core carries the C++ program, and a3-engine's history with it.

The program (Core, engine, window) used to live in a3-engine. Its history was
merged into this repository with hashes kept, so these checks pin what that
merge must leave behind: the history, the build entry points, the one
submodule, the version count from a3-core's tag, and the licence map.
"""

import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

ENGINE_COMMITS = (
    "70ccc5789287ca409885f990c5758ccd02e52ce6",
    "019d4da855d51a7faef4b9d3b1cf9d9f5194a69d",
)
IEM_PATH = "third_party/IEMPluginSuite"
IEM_URL = "https://git.iem.at/audioplugins/IEMPluginSuite.git"
IEM_COMMIT = "d9e0a9aac011ab1a1edfea1d5920918f159a5851"
CORE_TAG_COMMIT = "568ec01bb27426f85f31a2a5a9f89a2a8c9644f6"


def git(*args):
    return subprocess.run(("git", "-C", str(ROOT)) + args,
                          capture_output=True, text=True)


@unittest.skipUnless((ROOT / ".git").exists(),
                     "needs a git checkout (nuc-test mirrors without .git)")
class RepoCarriesTheProgramTest(unittest.TestCase):

    def test_the_engine_history_is_here(self):
        for commit in ENGINE_COMMITS:
            result = git("merge-base", "--is-ancestor", commit, "HEAD")
            self.assertEqual(result.returncode, 0, commit)

    def test_the_program_builds_from_the_root(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn("add_subdirectory(src/a3-core)", cmake)
        for script in ("test.sh", "build.sh", "packaging/stage"):
            path = ROOT / script
            self.assertTrue(path.is_file(), script)
            self.assertTrue(os.access(path, os.X_OK), script + " not executable")

    def test_iem_is_the_only_submodule(self):
        modules = (ROOT / ".gitmodules").read_text()
        self.assertEqual(modules.count("path = "), 1)
        self.assertIn("path = " + IEM_PATH, modules)
        self.assertIn("url = " + IEM_URL, modules)
        entry = git("ls-tree", "HEAD", IEM_PATH).stdout.split()
        self.assertEqual(entry[:3], ["160000", "commit", IEM_COMMIT])

    def test_versions_count_from_a3_cores_tag(self):
        described = git("describe", "--tags", "--long", "--match", "v*", "HEAD")
        self.assertTrue(described.stdout.startswith("v03.0-"), described.stdout)
        tagged = git("rev-list", "-n1", "v03.0").stdout.strip()
        self.assertEqual(tagged, CORE_TAG_COMMIT)

    def test_readme_is_a3_cores_and_program_md_the_engines(self):
        self.assertTrue((ROOT / "README.md").read_text().startswith("# A³ Core"))
        # PROGRAM.md began as the engine's README at the move; it may grow since
        # (a3-core --links, 2026-10-28), so its title and first paragraph are pinned.
        engine_readme = git("show", ENGINE_COMMITS[1] + ":README.md").stdout
        head = engine_readme.split("\n\n", 2)[:2]
        self.assertEqual((ROOT / "PROGRAM.md").read_text().split("\n\n", 2)[:2], head)

    def test_dep5_covers_the_program(self):
        text = (ROOT / ".reuse/dep5").read_text()
        stanzas = [s for s in text.split("\n\n") if s.startswith("Files:")]
        files = [" ".join(s.split("Copyright:")[0].split()[1:]) for s in stanzas]

        def index_of(pattern):
            hits = [i for i, f in enumerate(files) if pattern in f.split()]
            self.assertTrue(hits, pattern)
            return hits[0]

        index_of("src/*")
        index_of("tests/engine/reference/airwindows/*")
        self.assertGreater(index_of(IEM_PATH + "/*"), index_of("tools/*.json"))


if __name__ == "__main__":
    unittest.main()
