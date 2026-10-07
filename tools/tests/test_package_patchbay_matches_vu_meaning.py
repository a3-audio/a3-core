"""The `vu` meaning says which REAPER output each meter hears, and the
shipped patchbay agrees, plug for plug.

Issue a3-audio/a3-core#67: the meaning said "REAPER out 30 + n for 1-40",
while /vu 21-36 hear out11-26 (n - 10). The meaning carries the mapping as
one clause of range rules, "REAPER out per range: 1-20 = out n + 30, ...",
and this test reads that clause back. A rewrite that loses the clause fails
here loudly rather than going stale quietly.
"""

import json
import re
import unittest

from tools.tests.test_package_patchbay_wires_stereo_meters import TRUTH, wired_pairs

CLAUSE = re.compile(r"REAPER out per range: ([^;]*);")
RULE = re.compile(r"^(\d+)-(\d+) = out n(?: ([+-]) (\d+))?$")
FREE_PREFIX = "free"


def range_rules(meaning):
    """[(first, last, offset)] from the meaning's clause; a /vu n in
    first..last hears REAPER out n + offset."""
    match = CLAUSE.search(meaning)
    if not match:
        return []
    rules = []
    for text in match.group(1).split(", "):
        rule = RULE.match(text.strip())
        if not rule:
            raise ValueError(f"unreadable range rule: {text!r}")
        first, last, sign, amount = rule.groups()
        offset = int(amount or 0) * (-1 if sign == "-" else 1)
        rules.append((int(first), int(last), offset))
    return rules


def claimed_output(rules, number):
    for first, last, offset in rules:
        if first <= number <= last:
            return f"out{number + offset}"
    return None


def wired_output_by_meter(meters):
    """{/vu number: REAPER output} as the patchbay wires it."""
    numbers = {f"vu_{name}": number for number, name in enumerate(meters, start=1)}
    return {numbers[plug]: out for out, plug in wired_pairs() if plug in numbers}


class VuMeaningMatchesPatchbay(unittest.TestCase):
    def setUp(self):
        truth = json.loads(TRUTH.read_text())
        self.meters = truth["vu_meters"]
        self.rules = range_rules(truth["addresses"]["vu"]["meaning"])
        self.wired = wired_output_by_meter(self.meters)

    def test_the_meaning_states_its_ranges(self):
        self.assertTrue(self.rules, "no 'REAPER out per range: ...;' clause in the vu meaning")

    def test_the_ranges_do_not_overlap(self):
        numbers = [n for first, last, _ in self.rules for n in range(first, last + 1)]
        self.assertEqual(len(numbers), len(set(numbers)))

    def test_every_wired_meter_hears_the_output_the_meaning_names(self):
        for number, out in sorted(self.wired.items()):
            with self.subTest(meter=number, name=self.meters[number - 1]):
                self.assertEqual(claimed_output(self.rules, number), out)

    def test_every_named_meter_in_a_range_has_its_cable(self):
        for first, last, _ in self.rules:
            for number in range(first, last + 1):
                name = self.meters[number - 1]
                if name.startswith(FREE_PREFIX):
                    continue
                with self.subTest(meter=number, name=name):
                    self.assertIn(number, self.wired)


if __name__ == "__main__":
    unittest.main()
