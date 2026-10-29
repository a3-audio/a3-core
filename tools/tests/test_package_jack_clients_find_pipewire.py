"""Every JACK program on the Core talks to PipeWire, system-wide (2026-10-14).

pipewire-jack puts PipeWire's libjack in a directory of its own, out of the
loader's way; Debian leaves the switch to the admin and ships the ld.so.conf.d
file only as an example. The rig started with an LD_LIBRARY_PATH drop-in per
unit -- REAPER and QjackCtl then, the beat-analyzer, StemDeck -- and a JACK program
started by hand still found jackd's library. The program's package
(a3-core-app) ships Debian's example as the real file instead, so no unit
needs to know, and runs ldconfig so the loader's cache has it before anything
starts. Its name carries the building machine's multiarch triplet; the file
itself is packaging/app/00-a3-pipewire-jack.conf.in.
"""

import unittest

from tools.tests.paths import APP_DEBIAN, APP_RECIPE

TEMPLATE = APP_RECIPE / "00-a3-pipewire-jack.conf.in"
#: The name packaging/app/stage gives it on the NUCs.
LD_CONF_NAME = "00-a3-pipewire-jack-x86_64-linux-gnu.conf"
PIPEWIRE_JACK = "/usr/lib/@MULTIARCH@/pipewire-0.3/jack"


def maintainer_script(name):
    return (APP_DEBIAN / name).read_text()


class TheLoaderFindsPipeWiresLibjack(unittest.TestCase):
    def test_the_file_names_pipewire_jacks_directory(self):
        lines = [l.strip() for l in TEMPLATE.read_text().splitlines()
                 if l.strip() and not l.strip().startswith("#")]
        self.assertEqual([l.rstrip("/") for l in lines], [PIPEWIRE_JACK])

    def test_it_is_read_before_the_system_library_directory(self):
        """ld.so.conf reads ld.so.conf.d/*.conf in name order; the
        architecture's own directory comes from x86_64-linux-gnu.conf, where
        jackd2's libjack lives."""
        self.assertLess(LD_CONF_NAME, "x86_64-linux-gnu.conf")

    def test_it_is_no_conffile(self):
        """dpkg-deb refuses a conffile line whose file is not in the package,
        and the name differs per architecture; the file is generated, not
        edited, and goes with the package."""
        self.assertNotIn(LD_CONF_NAME, maintainer_script("conffiles"))

    def test_the_postinst_refreshes_the_loader_cache(self):
        self.assertRegex(maintainer_script("postinst"), r"(?m)^\s*ldconfig\b")

    def test_the_postrm_refreshes_it_on_remove_and_purge(self):
        postrm = maintainer_script("postrm")
        self.assertRegex(postrm, r"(?m)^\s*remove\|purge\)$")
        self.assertRegex(postrm, r"(?m)^\s*ldconfig\b")


if __name__ == "__main__":
    unittest.main()
