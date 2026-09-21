"""The package installs every program the units it starts will run.

**Why this exists.** zita-j2n and zita-n2j failed at every boot for as long as
anybody can tell, with systemd's status 203 -- "the program is not there".
DEBIAN/control listed `zita-ajbridge`, which bridges ALSA to JACK and ships
zita-a2j and zita-j2a; the units run the *network* bridge, `zita-njbridge`.
One letter apart, same author, and nothing compared the two. Found
2026-09-21, see issues/a3-core-zita-bridges-haben-nie-laufen-koennen.md.

Writing this down found a second one the same day: `jack_wait`, which
a3-reaper and qjackctl wait on since they stopped sleeping, comes from
`jack-example-tools` -- also absent from Depends. On a fresh install REAPER
would never have come up.

**What counts as started.** Everything reachable from default.target.wants
through `Wants=`. A unit that is shipped but that nothing starts --
a3-supercollider, today -- is not a promise, so a program it names is not
one either. Enable it and this starts asking about sclang.

**What it cannot check.** Which package really ships a program. That is
dpkg's answer, and it differs between releases, so it is written down in
PROVIDED_BY by hand. The test's job is narrower and still the one that
failed: a program the units run must have an entry there, and the entry's
package must be in Depends. A new program in a unit is a question this file
asks out loud instead of a boot that fails quietly.

Programs under /home are the project's own and are installed by it.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEBIAN = ROOT / "platform-config/debian-x86_64/a3-core/DEBIAN"
UNITS = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
         / "share/a3-core/config/systemd/user")

#: Present on every Debian system this package can be installed on, so a
#: Depends entry would say nothing. `Essential: yes` for all but procps, which
#: is priority important and in every installation that has a systemd user
#: session to begin with.
BASE_SYSTEM = "base system"

PROVIDED_BY = {
    "/bin/bash": BASE_SYSTEM,
    "/usr/bin/bash": BASE_SYSTEM,
    "/bin/sh": BASE_SYSTEM,
    "/bin/sleep": BASE_SYSTEM,
    "/usr/bin/sleep": BASE_SYSTEM,
    "/bin/true": BASE_SYSTEM,
    "/usr/bin/pkill": BASE_SYSTEM,
    "/usr/bin/jackd": "jackd2",
    "/usr/bin/jack_wait": "jack-example-tools",
    "/usr/bin/qjackctl": "qjackctl",
    "/usr/bin/zita-j2n": "zita-njbridge",
    "/usr/bin/zita-n2j": "zita-njbridge",
}

_EXEC_LINE = re.compile(r"^Exec\w*=[-@:+!]*(\S+)", re.MULTILINE)


def depends():
    """Package names in Depends, without versions and with every
    alternative of an `a | b` counted -- either one satisfies it."""
    control = (DEBIAN / "control").read_text()
    line = next(l for l in control.splitlines() if l.startswith("Depends:"))
    names = set()
    for entry in line.split(":", 1)[1].split(","):
        for alternative in entry.split("|"):
            names.add(alternative.split("(")[0].strip())
    return names


def wanted_by(unit_text):
    names = []
    for line in unit_text.splitlines():
        if line.startswith("Wants="):
            names.extend(line.split("=", 1)[1].split())
    return names


def started_units():
    """Shipped units reachable from default.target.wants through Wants=.
    Names that are not shipped here (a3-motion comes from its own repo) are
    followed no further -- they are not this package's to install for."""
    pending = sorted(p.name for p in (UNITS / "default.target.wants").iterdir())
    seen = set()
    while pending:
        name = pending.pop()
        path = UNITS / name
        if name in seen or not path.is_file():
            continue
        seen.add(name)
        pending.extend(wanted_by(path.read_text()))
    return seen


def programs_run_by(unit_names):
    programs = {}
    for name in unit_names:
        for program in _EXEC_LINE.findall((UNITS / name).read_text()):
            programs.setdefault(program, set()).add(name)
    return programs


class WhatIsStarted(unittest.TestCase):
    def test_the_bridges_and_the_whole_group_are_started(self):
        started = started_units()
        for unit in ("a3-main.service", "a3-jack.service",
                     "a3-reaper.service", "qjackctl.service",
                     "a3-core.service", "zita-j2n.service",
                     "zita-n2j.service"):
            self.assertIn(unit, started)

    def test_a_unit_nothing_starts_is_not_counted(self):
        self.assertNotIn("a3-supercollider.service", started_units())


class EveryProgramIsInstalled(unittest.TestCase):
    def setUp(self):
        self.programs = {
            program: units
            for program, units in programs_run_by(started_units()).items()
            if not program.startswith("/home/")
        }

    def test_every_program_a_started_unit_runs_is_accounted_for(self):
        unaccounted = {p: sorted(u) for p, u in self.programs.items()
                       if p not in PROVIDED_BY}
        self.assertEqual({}, unaccounted,
                         "a started unit runs a program nobody has said "
                         "where it comes from -- add it to PROVIDED_BY")

    def test_every_package_they_come_from_is_in_depends(self):
        declared = depends()
        missing = {
            program: PROVIDED_BY[program]
            for program in self.programs
            if program in PROVIDED_BY
            and PROVIDED_BY[program] != BASE_SYSTEM
            and PROVIDED_BY[program] not in declared
        }
        self.assertEqual({}, missing,
                         "DEBIAN/control does not install what the units run")


class ReadingControl(unittest.TestCase):
    def test_an_alternative_counts_as_declared(self):
        self.assertIn("jackd2", depends())


if __name__ == "__main__":
    unittest.main()
