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


# One token at a time, leftmost first: a literal is matched whole, so a comment marker inside one is
# text; a comment is replaced by its newlines. A quote after an alphanumeric is a digit separator
# (1'000), not a character literal.
TOKENS = re.compile(
    r"""R"(?P<delim>[^()\\\s]{0,16})\(.*?\)(?P=delim)"          # raw string
    |"(?:\\.|[^"\\\n])*"                                   # string
    |(?<![0-9A-Za-z_])'(?:\\.|[^'\\\n])*'                    # character
    |(?P<comment>/\*.*?\*/|//[^\n]*)""", re.S | re.X)
DISABLED = "DISABLED_"


def strip_inactive(text):
    """Drop comments and `#if 0` blocks (nesting-aware) so they cannot count."""
    text = TOKENS.sub(lambda m: "\n" * m.group(0).count("\n") if m.group("comment") else m.group(0), text)
    out, depth_off, depth = [], 0, 0
    for line in text.split("\n"):
        word = line.strip()
        if depth_off:
            if re.match(r"#\s*if", word):
                depth_off += 1
            elif re.match(r"#\s*endif", word):
                depth_off -= 1
            continue
        if re.match(r"#\s*if\s+0\b", word):
            depth_off = 1
            continue
        out.append(line)
    return "\n".join(out)


def collect_cpp_tests(dirs):
    """Return (enabled, disabled) sets of 'Suite.Name'; disabled lose DISABLED_."""
    enabled, disabled = set(), set()
    for d in dirs:
        for path in sorted(Path(d).glob("*.cpp")):
            text = strip_inactive(path.read_text(encoding="utf-8"))
            for suite, name in TEST_MACRO.findall(text):
                if name.startswith(DISABLED):
                    disabled.add(f"{suite}.{name[len(DISABLED):]}")
                else:
                    enabled.add(f"{suite}.{name}")
    return enabled, disabled


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
    cpp, disabled = collect_cpp_tests(test_dirs)
    missing, bad = [], []
    for row in read_cases(tsv, only, group):
        kind, _, rest = row["verdict"].partition(":")
        rest = rest.strip()
        if kind == "port":
            if rest in disabled and rest not in cpp:
                missing.append(f"{row['id']} -> {rest} (disabled in C++)")
            elif rest not in cpp:
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

    def test_a_commented_out_test_is_not_ported(self):
        tsv = self.tsv(("m.A.a", "port:S.Block", "stable", "", "3"),
                       ("m.A.b", "port:S.Line", "stable", "", "3"),
                       ("m.A.c", "port:S.Off", "stable", "", "3"),
                       ("m.A.d", "port:S.Live", "stable", "", "3"))
        dirs = self.cpp("/* TEST(S, Block) {}\n*/\n// TEST(S, Line) {}\n"
                        "#if 0\n#if X\nTEST(S, Off) {}\n#endif\nTEST(S, Off) {}\n#endif\n"
                        "TEST(S, Live) {}\n")
        missing, _ = check_parity(tsv, dirs)
        self.assertEqual(missing, ["m.A.a -> S.Block", "m.A.b -> S.Line", "m.A.c -> S.Off"])

    def test_a_comment_marker_inside_a_string_is_not_a_comment(self):
        tsv = self.tsv(("m.A.a", "port:S.After", "stable", "", "3"),
                       ("m.A.b", "port:S.Escaped", "stable", "", "3"),
                       ("m.A.c", "port:S.Raw", "stable", "", "3"),
                       ("m.A.d", "port:S.Char", "stable", "", "3"),
                       ("m.A.e", "port:S.Hidden", "stable", "", "3"))
        dirs = self.cpp('const char* a = "/channel/*/led/*";\nTEST(S, After) {}\n'
                        'const char* b = "quote \\" /* still a string";\nTEST(S, Escaped) {}\n'
                        'const char* c = R"x(a /* raw )" string)x";\nTEST(S, Raw) {}\n'
                        "char d = '\"'; int e = 1'000; const char* f = \"/*\";\nTEST(S, Char) {}\n"
                        "/* a real comment with \"a quote TEST(S, Hidden) {} */\n")
        missing, _ = check_parity(tsv, dirs)
        self.assertEqual(missing, ["m.A.e -> S.Hidden"])

    def test_a_disabled_test_is_not_ported(self):
        tsv = self.tsv(("m.A.a", "port:S.Name", "stable", "", "3"))
        missing, _ = check_parity(tsv, self.cpp("TEST(S, DISABLED_Name) {}\n"))
        self.assertEqual(len(missing), 1)
        self.assertIn("m.A.a -> S.Name", missing[0])
        self.assertIn("disabled", missing[0])

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
