"""Every JACK program on the Core talks to PipeWire, system-wide (2026-10-14).

pipewire-jack puts PipeWire's libjack in a directory of its own, out of the
loader's way; Debian leaves the switch to the admin and ships the ld.so.conf.d
file only as an example. The rig started with an LD_LIBRARY_PATH drop-in per
unit -- REAPER, QjackCtl, the beat-analyzer, StemDeck -- and a JACK program
started by hand still found jackd's library. The package ships Debian's
example as the real file instead, so no unit needs to know, and runs ldconfig
so the loader's cache has it before anything starts.
"""

import re
import unittest

from tools.tests.paths import DEBIAN, PACKAGE

LD_CONF = PACKAGE / "etc/ld.so.conf.d/pipewire-jack-x86_64-linux-gnu.conf"
PIPEWIRE_JACK = "/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack"


def maintainer_script(name):
    return (DEBIAN / name).read_text()


class TheLoaderFindsPipeWiresLibjack(unittest.TestCase):
    def test_the_file_names_pipewire_jacks_directory(self):
        lines = [l.strip() for l in LD_CONF.read_text().splitlines()
                 if l.strip() and not l.strip().startswith("#")]
        self.assertEqual([l.rstrip("/") for l in lines], [PIPEWIRE_JACK])

    def test_it_is_read_before_the_system_library_directory(self):
        """ld.so.conf reads ld.so.conf.d/*.conf in name order; the
        architecture's own directory comes from x86_64-linux-gnu.conf, where
        jackd2's libjack lives."""
        self.assertLess(LD_CONF.name, "x86_64-linux-gnu.conf")

    def test_it_is_a_conffile(self):
        self.assertIn("/etc/ld.so.conf.d/" + LD_CONF.name,
                      maintainer_script("conffiles").splitlines())

    def test_the_postinst_refreshes_the_loader_cache(self):
        self.assertRegex(maintainer_script("postinst"), r"(?m)^\s*ldconfig\b")

    def test_the_postrm_refreshes_it_on_purge(self):
        self.assertRegex(maintainer_script("postrm"), r"(?m)^\s*ldconfig\b")


if __name__ == "__main__":
    unittest.main()
