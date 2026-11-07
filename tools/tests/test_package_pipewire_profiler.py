"""The main PipeWire's profiler gathers a record every 10 ms, not on every graph cycle (2026-11-07).

The clock role reads the Scarlett's clock from `pw-profiler -J` (src/a3-clock). By default the profiler module
writes one record per cycle; at Block 64 that is 689 records a second, each carrying every node of the graph (five
AES67 senders included). The role drained them from a timer, the module's queue overflowed (`mod.profiler: queue
full`, 87 000 times in 18 minutes, from the real-time thread), the main PipeWire grew by 10 MB a minute, the
driver node collected errors, and the clock role lost records and ended in "fault". The role needs about one
measurement a second (ClockSteering.h, rateUpdates), so a record every 10 ms is a hundred times what it uses.
"""

import re
import unittest
from pathlib import Path

CONFIG = (Path(__file__).resolve().parents[2]
          / "platform-config/debian-x86_64/a3-core/home/aaa/.local/share/a3-core/config")
PIPEWIRE = CONFIG / "pipewire"
DROP_IN = PIPEWIRE / "pipewire.conf.d/20-a3-profiler.conf"

#: The clock role's design (PROGRAM.md): 20 records a second tell the rate to about 0.9 ppm rms over 5 s.
CLOCK_NEEDS_PER_SECOND = 20


def without_comments(text):
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def interval_ms():
    text = without_comments(DROP_IN.read_text())
    match = re.search(r"module\.profiler\.args\s*=\s*\{[^}]*?profile\.interval\.ms\s*=\s*(\d+)", text, re.DOTALL)
    return int(match.group(1)) if match else None


class TheProfilerIsNotAskedForEveryCycle(unittest.TestCase):
    def test_the_drop_in_is_shipped(self):
        self.assertTrue(DROP_IN.is_file(), f"{DROP_IN.relative_to(CONFIG)} is not shipped")

    def test_it_overrides_the_modules_arguments_and_gathers_every_10_ms(self):
        self.assertEqual(interval_ms(), 10)

    def test_the_clock_role_still_gets_more_records_than_it_needs(self):
        per_second = 1000 / interval_ms()
        self.assertGreaterEqual(per_second, CLOCK_NEEDS_PER_SECOND)
        self.assertLessEqual(per_second, 200, "far below one record per cycle at any block")

    def test_no_other_shipped_config_loads_the_profiler_again(self):
        for path in PIPEWIRE.rglob("*.conf"):
            if path != DROP_IN:
                with self.subTest(config=str(path.relative_to(PIPEWIRE))):
                    self.assertNotIn("libpipewire-module-profiler", without_comments(path.read_text()))


if __name__ == "__main__":
    unittest.main()
