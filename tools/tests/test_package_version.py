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
WORKFLOW = ROOT / ".github" / "workflows" / "pages-deploy.yml"

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


class TheWorkflowBuildsTheProgram(unittest.TestCase):
    """The script is only half of it: the workflow builds the program and the
    deployment into one .deb with the umbrella's installer/package.py, which
    reads the tags (so the history) and stamps the version itself. The YAML is
    read as text; no YAML library is a dependency here."""

    def setUp(self):
        self.text = WORKFLOW.read_text()

    def test_ci_builds_in_debian_testing(self):
        """The rig is Debian testing: dpkg-shlibdeps must name its libraries."""
        self.assertIn("runs-on: ubuntu-latest", self.text)
        self.assertIn("container: debian:testing", self.text)

    def test_ci_checks_out_history_and_the_iem_submodule(self):
        self.assertIn("fetch-depth: 0", self.text)
        self.assertIn("submodules: true", self.text)
        self.assertNotIn("recursive", self.text)
        self.assertIn("safe.directory '*'", self.text)

    def test_ci_installs_the_build_depends_file(self):
        self.assertIn("xargs -a packaging/build-depends apt-get install -y "
                      "--no-install-recommends", self.text)
        self.assertNotIn("sudo", self.text)
        git_first = self.text.find("apt-get install -y --no-install-recommends git ca-certificates")
        self.assertNotEqual(-1, git_first, "git must exist before checkout")
        self.assertLess(git_first, self.text.find("actions/checkout@v4"))

    def test_ci_builds_with_package_py_and_juce_9_0_3(self):
        self.assertIn("a3-audio/a3-system/main/installer/package.py", self.text)
        self.assertIn('python3 -I "$RUNNER_TEMP/package.py" "$GITHUB_WORKSPACE" '
                      "--rev HEAD", self.text)
        for flag in ("--out", "--cache", "--juce /opt/juce", "--jobs 4"):
            self.assertIn(flag, self.text)
        self.assertIn("--branch 9.0.3", self.text)
        self.assertNotIn("dpkg-deb --build", self.text)
        self.assertNotIn("package_version.py", self.text)

    def test_ci_caches_juce_and_ccache(self):
        self.assertIn("path: /opt/juce", self.text)
        self.assertIn("key: juce-9.0.3-debian-testing-v1", self.text)
        self.assertIn("path: /github/home/.ccache", self.text)
        self.assertIn("key: ccache-${{ github.sha }}", self.text)
        self.assertIn("restore-keys: ccache-", self.text)
        self.assertIn("CCACHE_DIR: /github/home/.ccache", self.text)

    def test_ci_publishes_repo_a3_core_deb(self):
        self.assertIn("repo/a3-core.deb", self.text)
        self.assertNotIn("platform-config/debian-x86_64/a3-core.deb", self.text)


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
