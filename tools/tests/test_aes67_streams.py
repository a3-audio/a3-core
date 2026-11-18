# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""The AES67 stream table in the truth (aes67_streams).

Every stream the rig sends is one row: its SDP name, multicast group, packet
time and where its channels come from. Core and StemDeck resolve the table
instead of carrying addresses of their own. A deck sends three streams, MAIN,
AUX and CUE (2026-11-18, the Core's way: one session each), every one in a block of
its own with one group per deck slot; the Core streams live in 239.69.200.x, clear
of every deck slot's group. The 12-channel "AES67 <host> out" (239.69.150.243) was retired on
2026-11-17: "AES67 Core REC" carries what it carried. Every packet has to fit one 1500-byte MTU frame.
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
DECK_KEYS = ["deck-main", "deck-aux", "deck-cue", "deck-stereo"]
# key: (session ends in, channels, first group); 1024 groups apart, so no slot up to ZZ (702) reaches the next.
DECK_STREAMS = {"deck-main": ("MAIN", 16, "239.69.160.0"),
                "deck-aux": ("AUX", 9, "239.69.164.0"),
                "deck-cue": ("CUE", 9, "239.69.168.0"),
                # "Output: Stereo" (a deck's setting) sends this one instead of the three.
                "deck-stereo": ("STEREO", 2, "239.69.172.0")}


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

    def test_the_table_exists_with_the_five_entries(self):
        self.assertEqual(
            [s["key"] for s in self.streams],
            DECK_KEYS + ["core-main", "core-booth", "core-rec", "core-phones"])

    def test_the_hosts_stream_is_gone(self):
        self.assertNotIn("legacy-out", self.by_key)
        names = {l["name"] for l in self.data["listeners"]}
        self.assertNotIn("aes67.stream", names)
        for s in self.streams:
            self.assertNotIn("{host}", s["session"], s["key"])
        core = [s for s in self.streams if s["sender"] == "core"]
        self.assertTrue(all(s["session"].startswith("AES67 Core ") for s in core))

    def test_sessions_and_groups_are_unique(self):
        sessions = [s["session"] for s in self.streams]
        groups = [resolved_group(s, self.hosts) for s in self.streams]
        self.assertEqual(len(sessions), len(set(sessions)))
        self.assertEqual(len(groups), len(set(groups)))

    def test_groups_are_multicast_in_the_a3_range(self):
        for s in self.streams:
            group = ipaddress.ip_address(resolved_group(s, self.hosts))
            self.assertIn(group, A3_RANGE, s["key"])

    def test_a_deck_sends_main_aux_and_cue_as_separate_sessions_and_stereo_as_a_fourth(self):
        for key, (name, channels, base) in DECK_STREAMS.items():
            with self.subTest(key=key):
                row = self.by_key[key]
                self.assertEqual(row["session"], "AES67 deck {deck} " + name)
                self.assertEqual(row["sender"], "deck")
                self.assertEqual(row["channels"], channels)
                self.assertEqual(row["source"], "fixed")
                self.assertEqual(row["group_base"], base)
                self.assertEqual(row["group_step"], 1)
                self.assertEqual(row["ptime"], 0.25)
                self.assertFalse(row["legacy"])
        self.assertNotIn("deck", self.by_key, "the 41-channel stream is gone")

    def test_the_new_core_groups_are_in_the_200_block(self):
        for i, key in enumerate(["core-main", "core-booth", "core-rec", "core-phones"], 1):
            self.assertEqual(self.by_key[key]["group"], "239.69.200.%d" % i, key)

    def test_no_deck_slot_up_to_zz_reaches_another_stream(self):
        taken = {ipaddress.ip_address(resolved_group(s, self.hosts))
                 for s in self.streams if "group_base" not in s}
        taken.add(ipaddress.ip_address(self.hosts["aes67-core"]))
        seen = set()
        for key in DECK_KEYS:
            deck = self.by_key[key]
            base = int(ipaddress.ip_address(deck["group_base"]))
            for index in range(702):   # A = 0 ... ZZ = 701
                group = ipaddress.ip_address(base + deck["group_step"] * index)
                self.assertIn(group, A3_RANGE, "%s slot %d" % (key, index))
                self.assertNotIn(group, taken, "%s slot %d" % (key, index))
                self.assertNotIn(group, seen, "%s slot %d" % (key, index))
                seen.add(group)

    def test_fixed_streams_fit_one_packet(self):
        fixed = [s for s in self.streams if s["source"] == "fixed"]
        self.assertTrue(fixed)
        for s in fixed:
            self.assertLessEqual(packet_bytes(s["channels"], s["ptime"]),
                                 MTU - IP_UDP_HEADERS, s["key"])

    def test_zone_and_pair_streams_fit_the_largest_layout(self):
        """The packet arithmetic only: 22 channels at 0.5 ms fit, 23 do not. The
        limit itself is enforced in C++ (resolveTxStreams drops the one stream
        above it; tests/core/Aes67Streams.cpp AZoneAboveThePacketLimitIsDroppedAloneAndSaid)."""
        self.assertLessEqual(packet_bytes(22, 0.5), MTU - IP_UDP_HEADERS)
        self.assertGreater(packet_bytes(23, 0.5), MTU - IP_UDP_HEADERS)
        self.assertLessEqual(packet_bytes(44, 0.25), MTU - IP_UDP_HEADERS)
        self.assertGreater(packet_bytes(45, 0.25), MTU - IP_UDP_HEADERS)
        for s in self.streams:
            if s["source"] != "fixed":
                self.assertEqual(s["ptime"], 0.5, s["key"])
                self.assertEqual(s["sender"], "core", s["key"])

    def test_the_deck_and_sap_listeners_are_still_there(self):
        """The old listener and hosts keys stay: a renderer from before the table finds a deck by them."""
        names = {l["name"]: l for l in self.data["listeners"]}
        self.assertEqual(names["aes67.deck"]["host"], "aes67-deck-{deck}")
        self.assertEqual(names["aes67.sap"]["port"], 9875)
        self.assertEqual(self.hosts["aes67-core"], "239.69.150.243")
        self.assertEqual(self.hosts["aes67-deck-a"], "239.69.150.244")


if __name__ == "__main__":
    unittest.main()
