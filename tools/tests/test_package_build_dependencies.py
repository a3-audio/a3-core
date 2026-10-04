"""What JUCE, StemDeck and Motion UI build against is in Depends.

A Core may build StemDeck and Motion UI (the a3-system installer decides),
and a3nuc2 needed twenty -dev packages for it, installed by hand over three
rounds of "fatal error: X11/Xlib.h" and "gsl not found" (2026-10-04). apt
cannot run inside the postinst -- dpkg holds the lock -- so they are listed
in Depends (decided 2026-10-04) rather than installed on demand.
"""

import re
import unittest
from pathlib import Path

CONTROL = (Path(__file__).resolve().parents[2]
           / "platform-config/debian-x86_64/a3-core/DEBIAN/control")

NEEDED = (
    # JUCE on Linux
    "libasound2-dev", "libx11-dev", "libxcomposite-dev", "libxcursor-dev",
    "libxext-dev", "libxinerama-dev", "libxrandr-dev", "libxrender-dev",
    "libfreetype-dev", "libfontconfig1-dev", "libglu1-mesa-dev",
    # StemDeck
    "libflac-dev", "libvorbis-dev", "libogg-dev", "libjack-jackd2-dev",
    # Motion UI and its V3 hardware interface
    "libgsl-dev", "libgpiod-dev", "libserial-dev",
    # building at all
    "cmake", "pkg-config", "git", "build-essential",
)


class BuildDependenciesAreDepends(unittest.TestCase):
    def test_they_are_listed(self):
        depends = re.search(r"^Depends: (.*)$", CONTROL.read_text(), re.MULTILINE).group(1)
        listed = {d.strip().split()[0] for d in depends.split(",")}
        self.assertEqual(set(), set(NEEDED) - listed)


if __name__ == "__main__":
    unittest.main()
