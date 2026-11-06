# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""The AES67 stream table in the truth (aes67_streams).

Every stream the rig sends is one row: its SDP name, multicast group, packet
time and where its channels come from. Core and StemDeck resolve the table
instead of carrying addresses of their own. The legacy groups (Core
239.69.150.243, decks from .244) must not move; the new Core streams live in
239.69.200.x, clear of every deck slot's group. Every packet has to fit one 1500-byte MTU frame.
"""

import ipaddress
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/lib"))

import a3_osc   # noqa: E402

TRUTH = ROOT / "platform-config/debian-x86_64/a3-core/usr/share/a3/a3-osc.json"
MTU = 1500
IP_UDP_HEADERS = 28
RTP_HEADER = 12
BYTES_PER_SAMPLE = 3   # L24
SAMPLE_RATE_KHZ = 44.1
A3_RANGE = ipaddress.ip_network("239.69.0.0/16")


def samples_per_packet(ptime_ms):
    return round(ptime_ms * SAMPLE_RATE_KHZ)


def packet_bytes(channels, ptime_ms):
    return channels * BYTES_PER_SAMPLE * samples_per_packet(ptime_ms) + RTP_HEADER


def resolved_group(entry, hosts):
    if "group_base" in entry:
        return entry["group_base"]
    return hosts.get(entry["group"], entry["group"])


class StreamTable(unittest.TestCase):
    def setUp(self):
        self.data = a3_osc.load(TRUTH).data()
        self.hosts = self.data["hosts"]
        self.streams = self.data["aes67_streams"]
        self.by_key = {s["key"]: s for s in self.streams}

    def test_the_table_exists_with_the_six_entries(self):
        self.assertEqual(
            [s["key"] for s in self.streams],
            ["legacy-out", "deck", "core-main", "core-booth", "core-rec", "core-phones"])

    def test_sessions_and_groups_are_unique(self):
        sessions = [s["session"] for s in self.streams]
        groups = [resolved_group(s, self.hosts) for s in self.streams]
        self.assertEqual(len(sessions), len(set(sessions)))
        self.assertEqual(len(groups), len(set(groups)))

    def test_groups_are_multicast_in_the_a3_range(self):
        for s in self.streams:
            group = ipaddress.ip_address(resolved_group(s, self.hosts))
            self.assertIn(group, A3_RANGE, s["key"])

    def test_the_legacy_addresses_did_not_move(self):
        self.assertEqual(resolved_group(self.by_key["legacy-out"], self.hosts), "239.69.150.243")
        self.assertEqual(self.by_key["legacy-out"]["group"], "aes67-core")
        self.assertEqual(self.by_key["deck"]["group_base"], "239.69.150.244")
        self.assertEqual(self.by_key["deck"]["group_step"], 1)
        self.assertTrue(self.by_key["legacy-out"]["legacy"])

    def test_the_new_core_groups_are_in_the_200_block(self):
        for i, key in enumerate(["core-main", "core-booth", "core-rec", "core-phones"], 1):
            self.assertEqual(self.by_key[key]["group"], "239.69.200.%d" % i, key)

    def test_no_deck_slot_up_to_zz_reaches_another_stream(self):
        deck = self.by_key["deck"]
        base = int(ipaddress.ip_address(deck["group_base"]))
        taken = {ipaddress.ip_address(resolved_group(s, self.hosts))
                 for s in self.streams if "group_base" not in s}
        taken.add(ipaddress.ip_address(self.hosts["aes67-core"]))
        for index in range(702):   # A = 0 ... ZZ = 701
            group = ipaddress.ip_address(base + deck["group_step"] * index)
            self.assertIn(group, A3_RANGE, "slot %d" % index)
            self.assertNotIn(group, taken, "slot %d" % index)

    def test_fixed_streams_fit_one_packet(self):
        fixed = [s for s in self.streams if s["source"] == "fixed"]
        self.assertTrue(fixed)
        for s in fixed:
            self.assertLessEqual(packet_bytes(s["channels"], s["ptime"]),
                                 MTU - IP_UDP_HEADERS, s["key"])

    def test_zone_and_pair_streams_fit_the_largest_layout(self):
        self.assertLessEqual(packet_bytes(22, 0.5), MTU - IP_UDP_HEADERS)
        self.assertGreater(packet_bytes(23, 0.5), MTU - IP_UDP_HEADERS)
        self.assertLessEqual(packet_bytes(44, 0.25), MTU - IP_UDP_HEADERS)
        self.assertGreater(packet_bytes(45, 0.25), MTU - IP_UDP_HEADERS)
        for s in self.streams:
            if s["source"] != "fixed":
                self.assertEqual(s["ptime"], 0.5, s["key"])
                self.assertEqual(s["sender"], "core", s["key"])

    def test_the_old_listeners_are_still_there(self):
        names = {l["name"]: l for l in self.data["listeners"]}
        self.assertEqual(names["aes67.stream"]["host"], "aes67-core")
        self.assertEqual(names["aes67.deck"]["host"], "aes67-deck-{deck}")
        self.assertEqual(names["aes67.sap"]["port"], 9875)
        self.assertEqual(self.hosts["aes67-core"], "239.69.150.243")
        self.assertEqual(self.hosts["aes67-deck-a"], "239.69.150.244")


if __name__ == "__main__":
    unittest.main()
