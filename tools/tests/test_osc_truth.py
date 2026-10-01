"""The one truth for OSC vocabulary, ports, IPs and the network.

Decided on 2026-09-30: one file, read by every device, instead of the same facts
in seven places (see .claude/notes/a3-osc-single-truth.md in the workspace). The
package ships it to /usr/share/a3/a3-osc.json; a3_osc.py is the one place that
reads it.

These tests pin what the file has to say, not how it is laid out: the network
as it stands on the rig, the hosts, no two programs on one port, and the
vocabulary after the clean-up of the same day -- channels counted from 1, the
filter named for what it is, hyphens, lamps under what they light, and the VU
map's forty meters on /vu/1..40.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
TRUTH = PACKAGE / "usr/share/a3/a3-osc.json"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc   # noqa: E402


def truth():
    return a3_osc.load(TRUTH)


class TheNetwork(unittest.TestCase):
    """As it stands on the rig (/etc/systemd/network, 2026-09-30)."""

    def test_the_bridge(self):
        network = truth().network()
        self.assertEqual(network["bridge"], "br0")
        self.assertEqual(network["bridge_ports"], ["eno1", "enp5s0"])
        self.assertTrue(network["stp"])

    def test_the_cores_address(self):
        network = truth().network()
        self.assertEqual(network["address"], "192.168.8.10/24")
        self.assertEqual(network["gateway"], "192.168.8.1")
        self.assertEqual(network["dns"], "192.168.8.1")


class TheHosts(unittest.TestCase):
    def test_every_machine_by_name(self):
        t = truth()
        self.assertEqual(t.host("core"), "192.168.8.10")
        self.assertEqual(t.host("mixer"), "192.168.8.11")
        self.assertEqual(t.host("radla"), "192.168.43.96")

    def test_the_cores_host_is_its_network_address(self):
        t = truth()
        self.assertEqual(t.host("core"), t.network()["address"].split("/")[0])


class ThePorts(unittest.TestCase):
    def test_who_listens_where(self):
        t = truth()
        self.assertEqual(t.port("core", "osc"), 9000)
        self.assertEqual(t.port("core", "reaper-feedback"), 9002)
        self.assertEqual(t.port("motion", "osc"), 7771)
        self.assertEqual(t.port("motion", "vu"), 7772)
        self.assertEqual(t.port("motion", "energy"), 7777)
        self.assertEqual(t.port("mixer", "osc"), 7772)
        self.assertEqual(t.port("beat-analyzer", "clock"), 7775)
        self.assertEqual(t.port("reaper", "osc"), 9001)
        self.assertEqual(t.port("dualdelay", "osc"), 1340)
        self.assertEqual(t.port("zita-n2j", "audio"), 65100)
        self.assertEqual(t.port("radla", "zita-n2j"), 55100)
        self.assertEqual(t.port("prolink", "announce"), 50000)

    def test_no_two_listeners_on_one_host_and_port(self):
        # "any", "local" and "core" are one machine: a port bound on all of
        # its interfaces collides with the same port bound on loopback.
        same_machine = {"any": "core", "local": "core"}
        seen = {}
        for listener in truth().listeners():
            host = same_machine.get(listener["host"], listener["host"])
            key = (host, listener["port"])
            self.assertNotIn(key, seen, f"{listener['name']} and {seen.get(key)}")
            seen[key] = listener["name"]

    def test_every_route_joins_known_programs(self):
        t = truth()
        names = {listener["name"] for listener in t.listeners()}
        programs = t.programs()
        for route in t.routes():
            self.assertIn(route["from"], programs, route)
            self.assertIn(route["to"], names, route)


class TheVocabulary(unittest.TestCase):
    def test_channels_count_from_one(self):
        t = truth()
        self.assertEqual(t.address("channel.volume", ch=1), "/channel/1/volume")
        self.assertEqual(t.index_range("channel.volume", "ch"), (1, 4))

    def test_the_filter_has_its_name(self):
        t = truth()
        self.assertEqual(t.address("filter.frequency"), "/filter/frequency")
        self.assertEqual(t.address("filter.resonance"), "/filter/resonance")
        self.assertEqual(t.address("filter.mode"), "/filter/mode")
        self.assertEqual(t.address("channel.filter", ch=2), "/channel/2/filter")
        self.assertEqual(t.address("channel.filter.frequency", ch=3),
                         "/channel/3/filter/frequency")
        self.assertEqual(t.address("channel.filter.q", ch=4), "/channel/4/filter/q")
        # the delay send keeps its name: "fx" now means the delay only
        self.assertEqual(t.address("channel.fx-send", ch=1), "/channel/1/fx-send")

    def test_lamps_stand_under_what_they_light(self):
        t = truth()
        self.assertEqual(t.address("channel.filter.led", ch=1), "/channel/1/filter/led")
        self.assertEqual(t.address("channel.pfl.led", ch=1), "/channel/1/pfl/led")
        self.assertEqual(t.address("filter.led"), "/filter/led")

    def test_the_master_section(self):
        t = truth()
        self.assertEqual(t.address("master.phones-mix"), "/master/phones-mix")
        self.assertEqual(t.address("master.phones-volume"), "/master/phones-volume")
        self.assertEqual(t.address("master.fx-return"), "/master/fx-return")

    def test_the_old_names_are_gone(self):
        patterns = {entry["pattern"] for entry in truth().addresses().values()}
        for old in ("/fx/frequency", "/fx/resonance", "/fx/mode", "/fx/led",
                    "/channel/{ch}/fx", "/channel/{ch}/pot_1", "/channel/{ch}/pot_2",
                    "/channel/{ch}/led/fx", "/channel/{ch}/led/pfl",
                    "/master/phones_mix", "/master/phones_volume", "/master/return"):
            self.assertNotIn(old, patterns)

    def test_forty_vu_meters_from_one(self):
        t = truth()
        self.assertEqual(t.address("vu", n=1), "/vu/1")
        self.assertEqual(t.index_range("vu", "n"), (1, 40))
        meters = t.vu_meters()
        self.assertEqual(len(meters), 40)
        self.assertEqual(meters[0], "in1_pre")
        self.assertEqual(meters[10], "main_sub")
        self.assertEqual(meters[39], "free70")

    def test_every_address_names_known_programs(self):
        t = truth()
        programs = t.programs()
        for key, entry in t.addresses().items():
            for who in entry["from"] + entry["to"]:
                self.assertIn(who, programs, key)


class TakingAnAddressApart(unittest.TestCase):
    """Core decides by an address's name, not by pieces of a string: match()
    is address() backwards."""

    def test_a_channel_address(self):
        self.assertEqual(truth().match("/channel/1/volume"),
                         ("channel.volume", {"ch": 1}))
        self.assertEqual(truth().match("/channel/4/filter/frequency"),
                         ("channel.filter.frequency", {"ch": 4}))

    def test_an_address_without_fields(self):
        self.assertEqual(truth().match("/master/fx-return"), ("master.fx-return", {}))

    def test_a_vu_meter(self):
        self.assertEqual(truth().match("/vu/40"), ("vu", {"n": 40}))

    def test_outside_the_range_is_no_match(self):
        self.assertIsNone(truth().match("/channel/0/volume"))
        self.assertIsNone(truth().match("/channel/5/volume"))
        self.assertIsNone(truth().match("/vu/41"))

    def test_an_old_or_unknown_address_is_no_match(self):
        for address in ("/fx/frequency", "/master/return", "/channel/1/pot_1",
                        "/nothing"):
            self.assertIsNone(truth().match(address), address)

    def test_match_and_address_agree_everywhere(self):
        t = truth()
        for key, entry in t.addresses().items():
            fields = {name: entry[name][0] for name in ("ch", "n") if name in entry}
            self.assertEqual(t.match(t.address(key, **fields)), (key, fields), key)


class TheStemWords(unittest.TestCase):
    """Stems on the desk (spec stem-routing-on-the-desk, 2026-10-01)."""

    def setUp(self):
        self.t = truth()

    def test_the_desk_turns_and_pushes(self):
        self.assertEqual(self.t.address("channel.stem.turn", ch=2), "/channel/2/stem/turn")
        self.assertEqual(self.t.address("fx-return.stem.turn"), "/fx-return/stem/turn")
        self.assertEqual(self.t.address("fx-return.stem.push"), "/fx-return/stem/push")

    def test_core_says_what_is_where(self):
        self.assertEqual(self.t.address("channel.stem", ch=4), "/channel/4/stem")
        self.assertEqual(self.t.address("fx-return.stem"), "/fx-return/stem")

    def test_who_speaks_and_who_hears(self):
        a = self.t.addresses()
        for key in ("channel.stem.turn", "fx-return.stem.turn", "fx-return.stem.push"):
            self.assertEqual((a[key]["from"], a[key]["to"]), (["mixer"], ["core"]), key)
        for key in ("channel.stem", "fx-return.stem"):
            self.assertEqual((a[key]["from"], a[key]["to"]), (["core"], ["mixer", "motion"]), key)

    def test_a_turn_is_matched_back(self):
        self.assertEqual(self.t.match("/channel/3/stem/turn"), ("channel.stem.turn", {"ch": 3}))


if __name__ == "__main__":
    unittest.main()
