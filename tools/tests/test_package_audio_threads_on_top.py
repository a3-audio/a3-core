"""The audio threads sit above everything else the package starts
(2026-10-06, on PipeWire since 2026-10-14).

On a3nuc1 (RT kernel, threadirqs) JACK logged about 106 late clients a minute
with no device xrun: the units raised whole processes with RR -- QjackCtl 83,
REAPER's GUI, video and OSC threads 75, the beat-analyzer 65 -- above the
clients' process threads. With the GUI threads back at SCHED_OTHER the late
clients were gone. That half still holds: no unit raises a whole process.

The audio server is PipeWire now. Its module-rt puts the data loop at FIFO 83,
which it may only do when limits.d gives @audio an rtprio above that; the JACK
clients' threads run below it (the engine's are set by the realtime preload,
src/jack-rt-priority.c, which the postinst builds). The USB IRQ threads stay
above it all: set_irq_prio puts xhci at FIFO 95.

The JACK clients a3-core starts itself were REAPER and QjackCtl until
2026-10-27; since then it is the engine role of the program
(packaging/a3-engine.service)."""

import configparser
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
CONFIG = PACKAGE / "home/aaa/.local/share/a3-core/config"
LIMITS = PACKAGE / "etc/security/limits.d/audio.conf"

USB_IRQ_PRIORITY = 95
#: PipeWire's data loop: module-rt's default rt.prio.
DATA_LOOP_PRIORITY = 83
CLIENT_UNITS = (ROOT / "packaging/a3-engine.service",)
# beat-analyzer: its own package's unit plus a3-core's drop-in, see
# test_package_analyzer_from_package.
SCHEDULING_KEYS = ("CPUSchedulingPolicy", "CPUSchedulingPriority")


def service(unit):
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read_string(unit.read_text())
    return parser["Service"]


def audio_rtprio():
    match = re.search(r"(?m)^@audio\s+-\s+rtprio\s+(\d+)\s*$", LIMITS.read_text())
    if match is None:
        raise AssertionError("limits.d gives @audio no rtprio")
    return int(match.group(1))


def shipped_pipewire_configs():
    return sorted((CONFIG / "pipewire").rglob("*.conf"))


class AudioThreadsOnTop(unittest.TestCase):
    def test_the_data_loop_may_run_at_its_priority(self):
        self.assertGreater(audio_rtprio(), DATA_LOOP_PRIORITY)
        self.assertLessEqual(audio_rtprio(), USB_IRQ_PRIORITY)

    def test_no_shipped_config_moves_the_data_loop(self):
        for path in shipped_pipewire_configs():
            with self.subTest(config=path.name):
                set_lines = [l for l in path.read_text().splitlines()
                             if re.match(r"\s*rt\.prio\s*=", l)]
                self.assertEqual(set_lines, [])

    def test_no_unit_raises_a_whole_process(self):
        for unit in CLIENT_UNITS:
            for key in SCHEDULING_KEYS:
                with self.subTest(unit=unit.name, key=key):
                    self.assertNotIn(key, service(unit))

    def test_the_units_keep_their_cpu_affinity(self):
        for unit in CLIENT_UNITS:
            with self.subTest(unit=unit.name):
                self.assertEqual(service(unit).get("CPUAffinity"), "1 2 3")


if __name__ == "__main__":
    unittest.main()
