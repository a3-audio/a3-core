"""Depends carries what a Core builds, and nothing another role builds.

The one thing a Core compiles is beat-analyzer: the package's
a3-user-install.service runs recipes/user_install.sh, which builds it from
~/a3-system/beat-analyzer with its build.sh. What that build needs is in
Depends.

What JUCE, StemDeck and Motion UI build against is not. It was, from
2026-10-04, because a postinst cannot run apt and a3nuc2 had needed twenty
-dev packages by hand. Decided 2026-10-05: that was the wrong place. The
a3-system installer's StemDeck and Motion roles install their own packages
with apt (a3-system installer/roles/base.py and the role modules), and a
machine with only the StemDeck role must not need a3-core at all. So the
lists live there now, and a3-core keeps only what a Core needs.
"""

import re
import unittest
from pathlib import Path

CONTROL = (Path(__file__).resolve().parents[2]
           / "platform-config/debian-x86_64/a3-core/DEBIAN/control")

#: beat-analyzer's build on the Core -- see beat-analyzer's CMakeLists.txt
#: (pkg_check_modules jack and samplerate, REQUIRED pkg-config; sndfile is
#: optional; BTrack and its kiss_fft are a git submodule, patched with
#: `patch`, which build-essential brings through dpkg-dev) and build.sh
#: (cmake, `pkg-config --exists jack`). Its tests are plain assert()s, no
#: GoogleTest.
BEAT_ANALYZER_BUILD = (
    "build-essential", "cmake", "pkg-config", "git",
    "libjack-jackd2-dev", "libsamplerate0-dev",
)

#: Only JUCE, StemDeck or Motion UI build against these; no unit, script,
#: postinst or beat-analyzer build of the Core uses them. The a3-system
#: installer's roles install them (installer/roles/base.py).
OTHER_ROLES_ONLY = (
    # JUCE on Linux
    "libasound2-dev", "libx11-dev", "libxcomposite-dev", "libxcursor-dev",
    "libxext-dev", "libxinerama-dev", "libxrandr-dev", "libxrender-dev",
    "libxi-dev", "libfreetype-dev", "libfontconfig1-dev", "libglu1-mesa-dev",
    "mesa-common-dev", "libegl-dev",
    # StemDeck
    "libflac-dev", "libvorbis-dev", "libogg-dev", "libcurl4-openssl-dev",
    "ladspa-sdk",
    # Motion UI, its tests and its V3 hardware interface
    "libgsl-dev", "libgpiod-dev", "libserial-dev", "libgtest-dev",
    "libgmock-dev",
    # StemDeck and Motion UI rebuild through it; beat-analyzer does not
    "ccache",
)


def listed():
    depends = re.search(r"^Depends: (.*)$", CONTROL.read_text(), re.MULTILINE).group(1)
    return {d.strip().split()[0] for d in depends.split(",")}


class DependsIsWhatACoreBuilds(unittest.TestCase):
    def test_beat_analyzer_builds(self):
        self.assertEqual(set(), set(BEAT_ANALYZER_BUILD) - listed())

    def test_other_roles_bring_their_own(self):
        self.assertEqual(set(), set(OTHER_ROLES_ONLY) & listed())


if __name__ == "__main__":
    unittest.main()
