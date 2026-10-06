"""JACK's process threads sit above everything else the package starts
(2026-10-06).

On a3nuc1 (RT kernel, threadirqs) JACK logged about 106 late clients a minute
with no device xrun. jackd ran with -P 19, so every client's process thread
was at FIFO 14, while the units raised whole processes with RR: QjackCtl 83,
REAPER's GUI, video and OSC threads 75, all beat-analyzer threads 65, zita 64.
GUI threads preempted the audio threads. With jackd's process thread at 80,
the clients' at 75 and the GUI threads back at SCHED_OTHER the next two
minutes had no late client at all.

The USB IRQ threads stay above it all: set_irq_prio puts xhci at FIFO 95."""

import configparser
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UNITS = ROOT / ("platform-config/debian-x86_64/a3-core/home/aaa/.local/share/"
                "a3-core/config/systemd/user")

USB_IRQ_PRIORITY = 95
JACK_UNIT = "a3-jack.service"
CLIENT_UNITS = ("a3-reaper.service", "qjackctl.service",
                "beat-analyzer.service", "zita-j2n.service",
                "zita-n2j.service")
SCHEDULING_KEYS = ("CPUSchedulingPolicy", "CPUSchedulingPriority")


def service(name):
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read_string((UNITS / name).read_text())
    return parser["Service"]


def jack_priority():
    match = re.search(r"\s-P\s*(\d+)\s", service(JACK_UNIT)["ExecStart"] + " ")
    if match is None:
        raise AssertionError("jackd is started without -P")
    return int(match.group(1))


class AudioThreadsOnTop(unittest.TestCase):
    def test_jack_runs_high_but_below_the_usb_irq_threads(self):
        self.assertGreaterEqual(jack_priority(), 70)
        self.assertLess(jack_priority(), USB_IRQ_PRIORITY)

    def test_no_unit_raises_a_whole_process(self):
        for name in (JACK_UNIT,) + CLIENT_UNITS:
            for key in SCHEDULING_KEYS:
                with self.subTest(unit=name, key=key):
                    self.assertNotIn(key, service(name))

    def test_the_units_keep_their_cpu_affinity(self):
        for name in (JACK_UNIT,) + CLIENT_UNITS:
            with self.subTest(unit=name):
                self.assertEqual(service(name).get("CPUAffinity"), "1 2 3")


if __name__ == "__main__":
    unittest.main()
