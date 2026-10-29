"""The package's version follows the repository's tags.

Every package the workflow ever published was 1.0.0: the version sat in
DEBIAN/control and nothing changed it. apt compares versions, not contents, so
a rig that had 1.0.0 installed never saw another update -- the Action ran green
and published new code nobody could get (2026-09-25).

Versions are tags in this system (v03.0 is the first, set in every repo at
once), so the package version is the last tag plus the commits since:
v03.0 with 63 commits on top is 03.0+63.
"""

import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent

sys.path.insert(0, str(TOOLS))
import package_version  # noqa: E402


def dpkg_newer(a, b):
    """Whether dpkg orders version a after version b."""
    return subprocess.run(["dpkg", "--compare-versions", a, "gt", b]).returncode == 0


class TheVersionIsTheTagAndTheCommitsSince(unittest.TestCase):
    def test_a_tag_with_commits_on_top(self):
        self.assertEqual("03.0+63", package_version.debian_version("v03.0-63-g2656b74"))

    def test_right_on_the_tag(self):
        self.assertEqual("03.0+0", package_version.debian_version("v03.0-0-gabcdef1"))

    def test_the_next_tag(self):
        self.assertEqual("03.1+2", package_version.debian_version("v03.1-2-g1234567"))

    def test_no_tag_is_an_error_not_a_version(self):
        """A version made up without a tag could sort below what a rig already
        has, and apt would never offer it. Better that the build stops."""
        for describe in ("", "2656b74", "03.0-1-gabc", "vfoo-1-gabc"):
            with self.assertRaises(ValueError, msg=describe):
                package_version.debian_version(describe)


class AptSeesEveryPushAsAnUpgrade(unittest.TestCase):
    def test_above_every_package_published_so_far(self):
        self.assertTrue(dpkg_newer("03.0+0", "1.0.0"))

    def test_another_commit_is_newer(self):
        self.assertTrue(dpkg_newer("03.0+64", "03.0+63"))
        self.assertTrue(dpkg_newer("03.0+100", "03.0+99"))

    def test_the_next_tag_is_newer_than_any_commit_before_it(self):
        self.assertTrue(dpkg_newer("03.1+0", "03.0+999"))


class FromARealRepository(unittest.TestCase):
    def git(self, *args):
        subprocess.run(["git", "-C", self.repo, *args], check=True,
                       capture_output=True)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = self.tmp.name
        self.git("init", "-q")
        self.git("config", "user.email", "test@example.invalid")
        self.git("config", "user.name", "test")
        for n in range(3):
            Path(self.repo, "f").write_text(str(n))
            self.git("add", "f")
            self.git("commit", "-q", "-m", f"c{n}")
            if n == 0:
                self.git("tag", "-a", "v03.0", "-m", "v03.0")

    def tearDown(self):
        self.tmp.cleanup()

    def test_it_reads_the_tag_and_counts_the_commits(self):
        self.assertEqual("03.0+2", package_version.version_of(self.repo))


class TheBuildDependsCoverTheInstaller(unittest.TestCase):
    def test_build_depends_covers_the_installers_juce_list(self):
        base = ROOT.parent / "installer" / "roles" / "base.py"
        if not base.is_file():
            self.skipTest("not inside the umbrella checkout")
        match = re.search(r"JUCE_PACKAGES = \((.*?)\n\)", base.read_text(), re.S)
        self.assertIsNotNone(match, "JUCE_PACKAGES not found")
        wanted = re.findall(r'"([^"]+)"', re.sub(r"#.*", "", match.group(1)))
        have = (ROOT / "packaging" / "build-depends").read_text().split()
        self.assertEqual([], [p for p in wanted if p not in have])


if __name__ == "__main__":
    unittest.main()
