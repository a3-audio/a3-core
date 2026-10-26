# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Parity guard: every Python test case of a3-core is ported to C++ or listed.

cases.tsv (tests/core/parity/) has one row per Python case: id, verdict,
group, from, task. `port:<Suite>.<Name>` must exist as TEST/TEST_F in
tests/core or tests/engine; `retired:` and `stays:` need a reason.

Env: A3_PARITY_ONLY=<module stem> narrows to one Python module,
     A3_PARITY_GROUP=stable|G narrows to one group.
"""
import csv
import os
import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CASES = ROOT / "tests/core/parity/cases.tsv"
TEST_DIRS = [ROOT / "tests/core", ROOT / "tests/engine"]
TEST_MACRO = re.compile(r"^\s*TEST(?:_F)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)", re.M)


def collect_cpp_tests(dirs):
    found = set()
    for d in dirs:
        for path in sorted(Path(d).glob("*.cpp")):
            for suite, name in TEST_MACRO.findall(path.read_text(encoding="utf-8")):
                found.add(f"{suite}.{name}")
    return found


def read_cases(tsv, only=None, group=None):
    with open(tsv, encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    if only:
        rows = [r for r in rows if r["id"].split(".")[0] == only]
    if group:
        rows = [r for r in rows if r["group"] == group]
    return rows


def check_parity(tsv, test_dirs, only=None, group=None):
    """Return (missing_ports, bad_verdicts) as lists of strings."""
    cpp = collect_cpp_tests(test_dirs)
    missing, bad = [], []
    for row in read_cases(tsv, only, group):
        kind, _, rest = row["verdict"].partition(":")
        rest = rest.strip()
        if kind == "port":
            if rest not in cpp:
                missing.append(f"{row['id']} -> {rest or '(no target)'}")
        elif kind in ("retired", "stays"):
            if not rest:
                bad.append(f"{row['id']}: {kind} without a reason")
        else:
            bad.append(f"{row['id']}: unknown verdict {row['verdict']!r}")
    return missing, bad


HEADER = "id\tverdict\tgroup\tfrom\ttask\n"


class GuardSelfTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)

    def tsv(self, *rows):
        path = self.dir / "cases.tsv"
        path.write_text(HEADER + "".join("\t".join(r) + "\n" for r in rows), encoding="utf-8")
        return path

    def cpp(self, text):
        (self.dir / "T.cpp").write_text(text, encoding="utf-8")
        return [self.dir]

    def test_a_missing_port_is_named(self):
        tsv = self.tsv(("test_a.A.test_x", "port:Suite.There", "stable", "abc", "3"),
                       ("test_a.A.test_y", "port:Suite.Gone", "stable", "abc", "3"))
        dirs = self.cpp("TEST(Suite, There) {}\nTEST_F(Other, Fx) {}\n")
        missing, bad = check_parity(tsv, dirs)
        self.assertEqual(missing, ["test_a.A.test_y -> Suite.Gone"])
        self.assertEqual(bad, [])
        dirs = self.cpp("TEST(Suite, There) {}\nTEST_F(Suite, Gone) {}\n")
        self.assertEqual(check_parity(tsv, dirs), ([], []))

    def test_retired_and_stays_need_a_reason(self):
        tsv = self.tsv(("m.A.a", "retired:", "stable", "", "3"),
                       ("m.A.b", "stays:", "stable", "", "3"),
                       ("m.A.c", "retired:HTML page is gone", "stable", "", "3"),
                       ("m.A.d", "stays:Python tool", "stable", "", "3"))
        missing, bad = check_parity(tsv, self.cpp(""))
        self.assertEqual(missing, [])
        self.assertEqual(len(bad), 2)
        self.assertIn("m.A.a", bad[0])
        self.assertIn("m.A.b", bad[1])

    def test_group_filter(self):
        tsv = self.tsv(("m1.A.a", "port:S.One", "stable", "", "3"),
                       ("m2.A.b", "port:S.Two", "G", "", "19"))
        dirs = self.cpp("")
        self.assertEqual(len(check_parity(tsv, dirs)[0]), 2)
        self.assertEqual(check_parity(tsv, dirs, group="stable")[0], ["m1.A.a -> S.One"])
        self.assertEqual(check_parity(tsv, dirs, group="G")[0], ["m2.A.b -> S.Two"])
        self.assertEqual(check_parity(tsv, dirs, only="m2")[0], ["m2.A.b -> S.Two"])


class RealParityCheck(unittest.TestCase):
    """The real check against cases.tsv; fails until every case is ported."""

    def test_every_python_case_is_ported_or_listed(self):
        missing, bad = check_parity(CASES, TEST_DIRS,
                                    only=os.environ.get("A3_PARITY_ONLY") or None,
                                    group=os.environ.get("A3_PARITY_GROUP") or None)
        self.assertEqual(bad, [], "malformed verdicts")
        self.assertEqual(missing, [],
                         f"{len(missing)} port: cases have no TEST/TEST_F:\n" + "\n".join(missing))


if __name__ == "__main__":
    unittest.main()
