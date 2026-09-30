"""Core takes its hosts and ports from the one truth, and from nowhere else.

The guard reads the code as a syntax tree rather than as text: a comment or a
docstring that tells the history ("the mixer used to be 192.168.43.61") is
documentation and may stay. What may not stay is a value -- an IP address or
one of the truth's port numbers written into the code, where it becomes a
second truth the moment the file changes.

a3_osc.py is the one module allowed to hold them: it is the reader.
"""

import ast
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOME = ROOT / "platform-config/debian-x86_64/a3-core/home/aaa"
TRUTH = ROOT / "platform-config/debian-x86_64/a3-core/usr/share/a3/a3-osc.json"

CORE_SOURCES = [HOME / ".local/bin/a3-core.py"] + sorted(
    path for path in (HOME / ".local/lib").glob("a3_core*.py"))

IPV4 = re.compile(r"\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b")


def truth_ports():
    """Core's ports: where it listens, and where its routes lead. Pro DJ Link's
    50000 is not Core's business, and a table capped at 50,000 rows is not a
    port."""
    data = json.loads(TRUTH.read_text())
    reached = {route["to"] for route in data["routes"] if route["from"] == "core"}
    return {listener["port"] for listener in data["listeners"]
            if listener["program"] == "core" or listener["name"] in reached}


def values(path):
    """Every constant in the module that is not a docstring."""
    tree = ast.parse(path.read_text())
    docstrings = set()
    for node in ast.walk(tree):
        body = getattr(node, "body", None)
        if isinstance(body, list) and body and isinstance(body[0], ast.Expr) \
                and isinstance(body[0].value, ast.Constant):
            docstrings.add(id(body[0].value))
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and id(node) not in docstrings:
            yield node


class CoreHoldsNoAddressOfItsOwn(unittest.TestCase):
    def test_no_ip_address_in_the_code(self):
        for path in CORE_SOURCES:
            for node in values(path):
                # "write 127.0.0.1:PORT" is a help text's example of the
                # format, not a connection.
                if isinstance(node.value, str) and ":PORT" not in node.value:
                    self.assertIsNone(IPV4.search(node.value),
                                      f"{path.name}:{node.lineno} {node.value!r}")

    def test_no_port_of_the_truth_in_the_code(self):
        ports = truth_ports()
        for path in CORE_SOURCES:
            for node in values(path):
                value = node.value
                if isinstance(value, bool):
                    continue
                if isinstance(value, int):
                    self.assertNotIn(value, ports, f"{path.name}:{node.lineno}")
                if isinstance(value, str):
                    for number in re.findall(r"(?<![\d.])(\d{4,5})(?![\d.])", value):
                        self.assertNotIn(int(number), ports,
                                         f"{path.name}:{node.lineno} {value!r}")


if __name__ == "__main__":
    unittest.main()
