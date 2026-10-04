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

import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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
        self.assertEqual(t.address("channel.aux-send", ch=1), "/channel/1/aux-send")

    def test_lamps_stand_under_what_they_light(self):
        t = truth()
        self.assertEqual(t.address("channel.filter.led", ch=1), "/channel/1/filter/led")
        self.assertEqual(t.address("channel.cue.led", ch=1), "/channel/1/cue/led")
        self.assertEqual(t.address("filter.led"), "/filter/led")

    def test_the_master_section(self):
        t = truth()
        self.assertEqual(t.address("master.phones-mix"), "/master/phones-mix")
        self.assertEqual(t.address("master.phones-volume"), "/master/phones-volume")
        self.assertEqual(t.address("master.aux-return"), "/master/aux-return")

    def test_the_old_names_are_gone(self):
        patterns = {entry["pattern"] for entry in truth().addresses().values()}
        for old in ("/fx/frequency", "/fx/resonance", "/fx/mode", "/fx/led",
                    "/channel/{ch}/fx", "/channel/{ch}/pot_1", "/channel/{ch}/pot_2",
                    "/channel/{ch}/led/fx", "/channel/{ch}/led/pfl",
                    "/master/phones_mix", "/master/phones_volume", "/master/return"):
            self.assertNotIn(old, patterns)

    def test_fifty_vu_meters_from_one(self):
        # 40 REAPER outs, the 8 stem pairs (issue a3-system#71), StemDeck's AUX bus
        t = truth()
        self.assertEqual(t.address("vu", n=1), "/vu/1")
        self.assertEqual(t.index_range("vu", "n"), (1, 50))
        meters = t.vu_meters()
        self.assertEqual(len(meters), 50)
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
        self.assertEqual(truth().match("/master/aux-return"), ("master.aux-return", {}))

    def test_a_vu_meter(self):
        self.assertEqual(truth().match("/vu/40"), ("vu", {"n": 40}))

    def test_outside_the_range_is_no_match(self):
        self.assertIsNone(truth().match("/channel/0/volume"))
        self.assertIsNone(truth().match("/channel/5/volume"))
        self.assertIsNone(truth().match("/vu/51"))

    def test_an_old_or_unknown_address_is_no_match(self):
        for address in ("/fx/frequency", "/master/return", "/channel/1/pot_1",
                        "/nothing"):
            self.assertIsNone(truth().match(address), address)

    def test_match_and_address_agree_everywhere(self):
        t = truth()
        for key, entry in t.addresses().items():
            fields = {name: entry[name][0] for name in ("ch", "n", "deck", "stem", "bus")
                      if name in entry}
            self.assertEqual(t.match(t.address(key, **fields)), (key, fields), key)


class TheStemMeters(unittest.TestCase):
    """StemDeck's 8 stereo stems, one meter each: /vu/41-48, sent by StemDeck
    itself since spec stemdeck-remote (first by the beat-analyzer, #71)."""

    def setUp(self):
        self.t = truth()

    def test_the_stems_follow_the_forty(self):
        meters = self.t.vu_meters()
        self.assertEqual(meters[40:48], ["stem_a1", "stem_a2", "stem_a3", "stem_a4",
                                       "stem_b1", "stem_b2", "stem_b3", "stem_b4"])

    def test_a_stem_meter_has_its_address(self):
        self.assertEqual(self.t.address("vu", n=48), "/vu/48")


class TheAuxBusMeters(unittest.TestCase):
    """StemDeck's AUX bus as a true stereo pair (decided 2026-10-04): the
    desk's aux-return display shows SA (these) beside A (aux_L/aux_R)."""

    def setUp(self):
        self.t = truth()

    def test_vu_reaches_fifty(self):
        self.assertEqual(self.t.addresses()["vu"]["n"], [1, 50])

    def test_the_aux_bus_follows_the_stems(self):
        meters = self.t.vu_meters()
        self.assertEqual(len(meters), 50)
        self.assertEqual(meters[48:], ["stem_aux_L", "stem_aux_R"])

    def test_meter_n_is_the_list_index_plus_one(self):
        meters = self.t.vu_meters()
        self.assertEqual(meters.index("stem_aux_L") + 1, 49)
        self.assertEqual(meters.index("stem_aux_R") + 1, 50)
        self.assertEqual(self.t.match("/vu/50"), ("vu", {"n": 50}))

    def test_stemdeck_sends_them_to_the_desk(self):
        self.assertIn("stemdeck", self.t.addresses()["vu"]["from"])
        self.assertIn({"from": "stemdeck", "to": "mixer.osc", "carries": "vu"},
                      self.t.routes())

    def test_the_meaning_names_the_aux_bus(self):
        self.assertIn("49-50 StemDeck's AUX bus", self.t.addresses()["vu"]["meaning"])


class TheCueWords(unittest.TestCase):
    """PFL is called cue since 2026-10-01, wire included; the stems have a
    cue of their own (the C field on the aux-return display)."""

    def setUp(self):
        self.t = truth()

    def test_a_channels_cue(self):
        self.assertEqual(self.t.address("channel.cue", ch=2), "/channel/2/cue")
        self.assertEqual(self.t.address("channel.cue.led", ch=2), "/channel/2/cue/led")


    def test_pfl_is_gone(self):
        self.assertNotIn("channel.pfl", self.t.addresses())
        self.assertIsNone(self.t.match("/channel/1/pfl"))


class TheStemWords(unittest.TestCase):
    """Stems on the desk (spec stem-routing-on-the-desk, 2026-10-01)."""

    def setUp(self):
        self.t = truth()

    def test_the_desk_turns_and_pushes(self):
        self.assertEqual(self.t.address("channel.stem.turn", ch=2), "/channel/2/stem/turn")
        self.assertEqual(self.t.address("aux-return.stem.turn"), "/aux-return/stem/turn")
        self.assertEqual(self.t.address("aux-return.stem.push"), "/aux-return/stem/push")

    def test_core_says_what_is_where(self):
        self.assertEqual(self.t.address("channel.stem", ch=4), "/channel/4/stem")
        self.assertEqual(self.t.address("aux-return.stem"), "/aux-return/stem")

    def test_who_speaks_and_who_hears(self):
        a = self.t.addresses()
        for key in ("channel.stem.turn", "aux-return.stem.turn", "aux-return.stem.push"):
            self.assertEqual((a[key]["from"], a[key]["to"]), (["mixer"], ["core"]), key)
        for key in ("channel.stem", "aux-return.stem"):
            self.assertEqual((a[key]["from"], a[key]["to"]), (["core"], ["mixer", "motion"]), key)

    def test_the_return_carries_its_cursor_and_all_eight_pairs(self):
        self.assertEqual(self.t.addresses()["aux-return.stem"]["args"], "i" * 9)

    def test_a_turn_is_matched_back(self):
        self.assertEqual(self.t.match("/channel/3/stem/turn"), ("channel.stem.turn", {"ch": 3}))


if __name__ == "__main__":
    unittest.main()


class TheStemDeckSpeaks(unittest.TestCase):
    """Spec stemdeck-remote (2026-10-01): the desk switches StemDeck's buses."""

    def setUp(self):
        self.truth = a3_osc.load(TRUTH)

    def test_core_sets_a_switch(self):
        self.assertEqual(self.truth.address("stemdeck.bus", deck=2, stem=4, bus=6),
                         "/stemdeck/2/4/bus/6")

    def test_stemdeck_reports_a_mask(self):
        self.assertEqual(self.truth.address("stemdeck.buses", deck=1, stem=1),
                         "/stemdeck/1/1/buses")

    def test_core_asks_for_everything(self):
        self.assertEqual(self.truth.address("stemdeck.recall"), "/stemdeck/recall")

    def test_stemdeck_listens_on_its_own_port(self):
        self.assertEqual(self.truth.endpoint("stemdeck", "osc")[1], 7780)

    def test_stemdeck_says_hello(self):
        self.assertIn("stemdeck", self.truth.addresses()["device.hello"]["from"])

    def test_the_stem_cue_words_are_gone(self):
        self.assertNotIn("stem.cue", self.truth.addresses())
        self.assertNotIn("stem.cue.led", self.truth.addresses())


class TheStemSelector(unittest.TestCase):
    """Spec desk-stem-selector (2026-10-02): turn selects, push loads."""

    def setUp(self):
        self.truth = a3_osc.load(TRUTH)

    def test_a_push_loads(self):
        self.assertEqual(self.truth.address("channel.stem.push", ch=3), "/channel/3/stem/push")
        self.assertEqual(self.truth.addresses()["channel.stem.push"]["to"], ["core"])

    def test_core_announces_the_cursor(self):
        """2026-10-04: the menu became an input selector, one cursor 0-8."""
        self.assertEqual(self.truth.address("channel.stem.cursor", ch=1),
                         "/channel/1/stem/cursor")
        entry = self.truth.addresses()["channel.stem.cursor"]
        self.assertEqual((entry["from"], entry["args"]), (["core"], "i"))
        self.assertNotIn("channel.stem.menu", self.truth.addresses())
        self.assertNotIn("channel.stem.selected", self.truth.addresses())


class CoreAnnouncesItself(unittest.TestCase):
    """Spec truth-from-core (2026-10-02): devices find Core by its broadcast."""

    def setUp(self):
        self.truth = a3_osc.load(TRUTH)

    def test_every_device_listens_for_core_on_one_port(self):
        self.assertEqual(self.truth.port("devices", "announce"), 7790)

    def test_core_says_where_its_truth_is(self):
        self.assertEqual(self.truth.address("core.here"), "/core/here")
        self.assertEqual(self.truth.addresses()["core.here"]["args"], "ss")
        self.assertEqual(self.truth.addresses()["core.here"]["from"], ["core"])


class TheInstalledTruthIsJoined(unittest.TestCase):
    """Spec truth-from-core: the network part is the maintainer's file."""

    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)
        self.network = self.dir / "network.json"
        package = json.loads(TRUTH.read_text())
        hosts = dict(package["hosts"], mixer="10.9.9.9")
        self.network.write_text(json.dumps({"hosts": hosts, "network": package["network"]}))

    def test_a3_network_names_the_file_to_join(self):
        with mock.patch.dict(os.environ, {"A3_NETWORK": str(self.network)}):
            self.assertEqual(a3_osc.load(TRUTH).host("mixer"), "10.9.9.9")

    def test_an_explicit_truth_reads_no_network_file(self):
        with mock.patch.dict(os.environ, {}):
            os.environ.pop("A3_NETWORK", None)
            self.assertEqual(a3_osc.load(TRUTH).host("mixer"), "192.168.8.11")

    def test_a_broken_network_file_leaves_the_defaults_and_says_why(self):
        self.network.write_text("{")
        with mock.patch.dict(os.environ, {"A3_NETWORK": str(self.network)}):
            truth = a3_osc.load(TRUTH)
        self.assertEqual(truth.host("mixer"), "192.168.8.11")
        self.assertTrue(truth.network_problem)

    def test_the_fingerprint_is_of_the_joined_truth(self):
        with mock.patch.dict(os.environ, {"A3_NETWORK": str(self.network)}):
            joined = a3_osc.load(TRUTH)
        self.assertNotEqual(joined.fingerprint(), a3_osc.load(TRUTH).fingerprint())
        self.assertEqual(json.loads(joined.canonical())["hosts"]["mixer"], "10.9.9.9")
