"""The truth joined from the package's contract and the maintainer's network
file (spec truth-from-core, 2026-10-02)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_osc_join import canonical, fingerprint, join, read_network  # noqa: E402

CONTRACT = {"version": 1, "hosts": {"core": "192.168.8.10"},
            "network": {"address": "192.168.8.10/24"}, "listeners": []}
MINE = {"hosts": {"core": "10.0.0.2"}, "network": {"address": "10.0.0.2/24"}}


class TheJoin(unittest.TestCase):
    def test_the_network_comes_from_the_maintainers_file(self):
        joined = join(CONTRACT, MINE)
        self.assertEqual(joined["hosts"], {"core": "10.0.0.2"})
        self.assertEqual(joined["network"], {"address": "10.0.0.2/24"})
        self.assertEqual(joined["listeners"], [])

    def test_without_a_file_the_contracts_blocks_stay(self):
        self.assertEqual(join(CONTRACT, None), CONTRACT)

    def test_the_contract_is_not_changed(self):
        join(CONTRACT, MINE)
        self.assertEqual(CONTRACT["hosts"], {"core": "192.168.8.10"})


class AnIncompleteFile(unittest.TestCase):
    """Final review 2026-10-02: a host deleted from the maintainer's file, or
    one a later package adds, must come from the package's defaults --
    replacing the whole block made Core fail to start."""

    def test_a_host_missing_from_the_file_comes_from_the_package(self):
        contract = dict(CONTRACT, hosts={"core": "192.168.8.10", "local": "127.0.0.1"})
        joined = join(contract, {"hosts": {"core": "10.0.0.2"}, "network": {}})
        self.assertEqual(joined["hosts"], {"core": "10.0.0.2", "local": "127.0.0.1"})

    def test_a_network_key_missing_from_the_file_comes_from_the_package(self):
        contract = dict(CONTRACT, network={"address": "192.168.8.10/24", "gateway": "192.168.8.1"})
        joined = join(contract, {"hosts": {}, "network": {"address": "10.0.0.2/24"}})
        self.assertEqual(joined["network"], {"address": "10.0.0.2/24", "gateway": "192.168.8.1"})


class TheFingerprint(unittest.TestCase):
    def test_key_order_does_not_matter(self):
        a = {"b": 1, "a": {"y": 2, "x": 3}}
        b = {"a": {"x": 3, "y": 2}, "b": 1}
        self.assertEqual(fingerprint(a), fingerprint(b))
        self.assertEqual(canonical(a), b'{"a":{"x":3,"y":2},"b":1}')

    def test_a_changed_value_changes_it(self):
        self.assertNotEqual(fingerprint(join(CONTRACT, MINE)), fingerprint(CONTRACT))

    def test_it_is_sha256_hex(self):
        self.assertRegex(fingerprint(CONTRACT), r"^[0-9a-f]{64}$")


class TheMaintainersFile(unittest.TestCase):
    def write(self, text):
        handle = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
        handle.write(text)
        handle.close()
        self.addCleanup(Path(handle.name).unlink)
        return Path(handle.name)

    def test_a_good_file_is_read(self):
        network, problem = read_network(self.write(json.dumps(MINE)))
        self.assertEqual((network, problem), (MINE, None))

    def test_a_missing_file_is_no_problem_just_absent(self):
        self.assertEqual(read_network(Path("/nonexistent/a3/network.json")), (None, None))

    def test_a_broken_file_is_refused_with_a_reason(self):
        for text in ("{", "[]", json.dumps({"hosts": {}}), json.dumps({"hosts": [], "network": {}})):
            network, problem = read_network(self.write(text))
            self.assertIsNone(network, text)
            self.assertTrue(problem, text)
