"""The Core's audio server is PipeWire, not jackd (2026-10-14).

The rig has run PipeWire with wireplumber since 2026-10-13, the Scarlett on
its pro-audio profile, and JACK programs talking to it through pipewire-jack.
jackd stays out entirely: a JACK server next to PipeWire is what took the
Scarlett away from it on 2026-10-13, when a unit still bound to a3-jack
started jackd. So nothing the package ships starts, wants or waits for
a3-jack any more, and what came after it comes after PipeWire.
"""

import configparser
import re
import unittest

from tools.tests.paths import DEBIAN, PACKAGE, ROOT

CONTROL = DEBIAN / "control"
CONFIG = PACKAGE / "home/aaa/.local/share/a3-core/config"
UNITS = CONFIG / "systemd/user"

#: The units (or a3-core's drop-ins on units of other packages) that came
#: after a3-jack: they need the audio graph.
AUDIO_UNITS = (
    UNITS / "a3-core.service",
    ROOT / "packaging/a3-engine.service",
    UNITS / "beat-analyzer.service.d/a3-core.conf",
    UNITS / "stemdeck.service.d/a3-core.conf",
)


def depends():
    line = next(l for l in CONTROL.read_text().splitlines() if l.startswith("Depends:"))
    return {entry.split("(")[0].strip() for entry in line.split(":", 1)[1].split(",")}


def unit_section(path, name):
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read_string(path.read_text())
    return parser[name] if parser.has_section(name) else {}


def unit_lines(path):
    """A unit's lines without its comments: what systemd reads."""
    return [line for line in path.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith(("#", ";"))]


def shipped_units():
    return [p for p in UNITS.rglob("*") if p.is_file() and not p.is_symlink()]


class DependsOnPipeWire(unittest.TestCase):
    def test_pipewire_its_jack_library_and_the_session_manager_are_installed(self):
        for package in ("pipewire", "pipewire-jack", "wireplumber"):
            self.assertIn(package, depends())

    def test_ptp_is_installed_for_aes67(self):
        self.assertIn("linuxptp", depends())

    def test_no_jack_server_and_no_zita_bridge_is_installed(self):
        for package in ("jackd2", "zita-njbridge", "zita-ajbridge"):
            self.assertNotIn(package, depends())

    def test_the_jack_headers_stay_for_builds(self):
        self.assertIn("libjack-jackd2-dev", depends())

    def test_qjackctl_went_with_its_patchbay(self):
        """Every cable of the patchbay had a REAPER end; the engine makes its
        own links (2026-10-27)."""
        self.assertNotIn("qjackctl", depends())


class NothingStartsJackd(unittest.TestCase):
    def test_the_jack_server_unit_is_not_shipped(self):
        self.assertFalse((UNITS / "a3-jack.service").exists())

    def test_no_shipped_unit_names_it(self):
        for path in shipped_units():
            with self.subTest(unit=str(path.relative_to(UNITS))):
                self.assertEqual([l for l in unit_lines(path) if "a3-jack" in l], [])

    def test_no_shipped_unit_runs_jackd(self):
        for path in shipped_units():
            with self.subTest(unit=str(path.relative_to(UNITS))):
                self.assertEqual(
                    [l for l in unit_lines(path) if re.search(r"\bjackd\b", l)], [])

    def test_no_shipped_link_points_at_it(self):
        for link in UNITS.rglob("*"):
            if link.is_symlink():
                self.assertNotIn("a3-jack", link.name)


class AudioUnitsComeAfterPipeWire(unittest.TestCase):
    def test_each_comes_after_pipewire_and_wireplumber(self):
        for path in AUDIO_UNITS:
            with self.subTest(unit=str(path.relative_to(ROOT))):
                after = unit_section(path, "Unit").get("After", "").split()
                self.assertIn("pipewire.service", after)
                self.assertIn("wireplumber.service", after)

    def test_none_needs_its_own_library_path(self):
        """pipewire-jack's libjack is found system-wide (ld.so.conf.d), so no
        unit points LD_LIBRARY_PATH at it -- a JACK program started by hand
        talks to PipeWire just the same."""
        for path in shipped_units():
            with self.subTest(unit=str(path.relative_to(UNITS))):
                self.assertNotIn("LD_LIBRARY_PATH", "\n".join(unit_lines(path)))


if __name__ == "__main__":
    unittest.main()
