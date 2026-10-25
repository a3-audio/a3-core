"""PipeWire runs the Core at 44.1 kHz and a block of 128 samples (since 2026-10-25; 64 from 2026-10-17 to 2026-10-25,
128 from 2026-10-14).

At 64 REAPER took 1.0-1.1 ms of a 1.45 ms cycle after a restart on 2026-10-25 and the Scarlett
counted about 160 xruns a second; at 128 the counter stood still.

The rig runs at 44.1 kHz end to end -- REAPER, the stems, the AES67 streams --
and a driver at 48 kHz would resample every packet. The block never goes below
128 samples: the AES67 nodes share their PTP clock with every node connected
to them, and their 0.5 ms packets would otherwise pull the whole graph down to
32 samples, more than REAPER and the Scarlett keep up with. Both PipeWire
instances -- the session's and the AES67 one -- get the same settings.

JACK programs see the Scarlett as "system", so the patchbay's
system:playback_N and system:capture_N fit as they did on jackd.
"""

import re
import unittest
from pathlib import Path

CONFIG = (Path(__file__).resolve().parents[2]
          / "platform-config/debian-x86_64/a3-core/home/aaa/.local/share/a3-core/config")
PIPEWIRE = CONFIG / "pipewire"
RATE_FILES = (PIPEWIRE / "pipewire.conf.d/10-a3-rate.conf",
              PIPEWIRE / "pipewire-aes67.conf.d/10-a3-rate.conf")
JACK_FILE = PIPEWIRE / "jack.conf.d/10-a3.conf"

RATE = 44100
BLOCK = 128


def setting(path, key):
    """The value of `key = value` in a PipeWire config, comments left out."""
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0]
        match = re.match(rf"\s*{re.escape(key)}\s*=\s*(.+?)\s*$", line)
        if match:
            return match.group(1)
    raise AssertionError(f"{path.name} does not set {key}")


class BothInstancesRunTheRigsClock(unittest.TestCase):
    def test_the_rate_is_44_1_khz_and_nothing_else(self):
        for path in RATE_FILES:
            with self.subTest(config=str(path.relative_to(PIPEWIRE))):
                self.assertEqual(int(setting(path, "default.clock.rate")), RATE)
                self.assertEqual(setting(path, "default.clock.allowed-rates"), f"[ {RATE} ]")

    def test_the_block_is_128_and_never_smaller(self):
        for path in RATE_FILES:
            with self.subTest(config=str(path.relative_to(PIPEWIRE))):
                self.assertEqual(int(setting(path, "default.clock.quantum")), BLOCK)
                self.assertEqual(int(setting(path, "default.clock.min-quantum")), BLOCK)


class JackProgramsSeeTheRig(unittest.TestCase):
    def test_the_jack_block_is_the_graphs(self):
        self.assertEqual(setting(JACK_FILE, "node.latency"), f"{BLOCK}/{RATE}")

    def test_the_sound_card_answers_as_system(self):
        self.assertEqual(setting(JACK_FILE, "jack.default-as-system"), "true")


if __name__ == "__main__":
    unittest.main()
