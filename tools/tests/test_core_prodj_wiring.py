# SPDX-FileCopyrightText: 2026 Patric Schmitz, Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Core follows the pro-DJ app: positions to it, where devices run, LIVE as a
lease, pointers only while LIVE. The decisions are in test_core_prodj_live and
test_core_desks; this holds that a3-core.py asks them and acts on the answer.
Read off the source: Core itself cannot be started in a test."""

import ast
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/bin/a3-core.py").read_text()
TRUTH = ROOT / "platform-config/debian-x86_64/a3-core/usr/share/a3/a3-osc.json"


def _source_of(function_name):
    node = next(n for n in ast.walk(ast.parse(CORE))
                if isinstance(n, ast.FunctionDef) and n.name == function_name)
    return ast.get_source_segment(CORE, node)


class Joining(unittest.TestCase):
    def test_a_prodj_hello_joins_as_a_desk_of_its_kind(self):
        source = _source_of("osc_handler_device_hello")
        self.assertIn('device in ("desk", "prodj") and their_hash == _devices.own_hash', source)
        self.assertIn("desk_said_hello(client_address[0], now, kind=device)", source)

    def test_a_joining_prodj_is_told_positions_places_and_live(self):
        source = _source_of("desk_said_hello")
        self.assertIn("PRODJ", source)
        self.assertIn("tell_prodj(", source)

    def test_the_client_is_named_by_its_kind(self):
        self.assertIn("desk_name(host, kind)", _source_of("desk_said_hello"))

    def test_the_prodj_clients_are_the_desks_of_that_kind(self):
        source = _source_of("prodj_clients")
        self.assertIn("_desks.hosts_of_kind(PRODJ)", source)
        self.assertIn("_desk_clients", source)

    def test_a_recall_answers_the_asker_with_live(self):
        source = _source_of("osc_handler_recall")
        self.assertIn("prodj.live", source)
        self.assertIn("origin_of(client_address)", source)
        self.assertIn("tell_prodj(", source)

    def test_stemdeck_news_tells_it_live(self):
        source = _source_of("stemdeck_said_hello")
        self.assertIn("prodj.live", source)
        self.assertGreater(source.index("prodj.live"), source.index("stemdeck.recall"))


class WhatAProdjIsTold(unittest.TestCase):
    def test_tell_prodj_sends_positions_places_and_live(self):
        source = _source_of("tell_prodj")
        self.assertIn("channel.azimuth", source)
        self.assertIn("channel.elevation", source)
        self.assertIn("is not None", source)
        self.assertIn("core.device-at", source)
        self.assertIn("prodj.live", source)

    def test_positions_reach_the_prodj_after_iem(self):
        for branch in ("azimuth", "elevation"):
            source = _source_of("osc_handler_channel")
            start = source.index(f'parameter == "{branch}"')
            part = source[start:source.index("elif", start + 10)]
            self.assertIn("for client in udp_clients_iem", part, branch)
            self.assertIn("send_position_to_prodj(key, channel_index,", part, branch)
            self.assertGreater(part.index("send_position_to_prodj("),
                               part.index("udp_clients_iem"), branch)
        self.assertIn("prodj_clients()", _source_of("send_position_to_prodj"))

    def test_motion_and_stemdeck_moving_tell_the_prodj(self):
        for function in ("motion_said_hello", "stemdeck_said_hello"):
            source = _source_of(function)
            self.assertIn("tell_prodj_where(", source, function)
        where = _source_of("tell_prodj_where")
        self.assertIn("core.device-at", where)
        self.assertIn("prodj_clients()", where)

    def test_a_place_on_this_machine_is_told_as_the_core_host(self):
        helper = _source_of("device_host")
        self.assertIn("host_to_tell_of(", helper)
        self.assertIn('_truth.host("core")', helper)
        self.assertIn("MOTION_OWN_HOSTS", helper)
        self.assertIn("device_host(host)", _source_of("tell_prodj_where"))
        self.assertIn("device_host(host)", _source_of("tell_prodj"))

    def test_motions_place_is_told_only_from_a_motion_heard(self):
        """Not the default target: before a hello, or after a silence, no
        Motion is known to run anywhere (a3_core_motion follows/followed)."""
        told = _source_of("tell_prodj")
        self.assertNotIn('PEER_HOSTS.get("motion")', told)
        self.assertIn("_motion.followed", told)
        hello = _source_of("motion_said_hello")
        self.assertIn("if _motion.follows(host):", hello)
        self.assertIn('tell_prodj_where("motion", host)', hello)
        self.assertNotIn("tell_prodj_where(", _source_of("point_motion_at"))

    def test_stemdecks_place_is_told_only_from_a_stemdeck_heard(self):
        told = _source_of("tell_prodj")
        self.assertIn("_stemdeck_watch.host if _stemdeck_client is not None else None", told)
        silence = _source_of("notice_stemdeck_silence")
        self.assertIn("_stemdeck_client = None", silence)
        self.assertNotIn("tell_prodj_where(", silence)


class Live(unittest.TestCase):
    def test_a_known_desk_saying_hello_again_is_answered_with_live(self):
        source = _source_of("desk_said_hello")
        known, joined = source.index("if verdict == KNOWN"), source.index("if verdict != JOINED")
        self.assertLess(known, joined)
        self.assertIn("prodj.live", source[known:joined])

    def test_motions_hello_is_answered_with_live(self):
        source = _source_of("motion_said_hello")
        self.assertIn("prodj.live", source)
        self.assertIn("osc_a3motion", source)

    def test_refused_pointers_are_seen_by_the_traffic_table(self):
        source = _source_of("osc_handler_pointer")
        self.assertNotIn("_pointers_refused", CORE)
        self.assertLess(source.index("traffic.seen("), source.index("_prodj_live.admits("))

    def test_live_is_switched_only_by_who_may(self):
        source = _source_of("osc_handler_prodj_live")
        self.assertIn("origin = origin_of(client_address)", source)
        self.assertIn("may_switch(origin,", source)
        self.assertIn("_prodj_live.set(", source)
        self.assertIn("announce_live(", source)

    def test_a_switch_is_said_in_the_journal(self):
        self.assertIn("prodj: LIVE", _source_of("osc_handler_prodj_live"))

    def test_live_is_repeated_on_the_tick_while_on(self):
        source = _source_of("on_tick")
        self.assertIn("_prodj_live.due(", source)
        self.assertIn("announce_live(", source)

    def test_due_is_asked_once_per_tick(self):
        self.assertEqual(_source_of("on_tick").count("_prodj_live.due("), 1)

    def test_the_last_prodj_leaving_switches_live_off(self):
        self.assertIn("live_off_when_no_prodj_is_left()", _source_of("notice_desk_silence"))
        source = _source_of("live_off_when_no_prodj_is_left")
        self.assertIn("_desks.hosts_of_kind(PRODJ)", source)
        self.assertIn("_prodj_live.nobody_left(", source)
        self.assertIn("announce_live(", source)
        self.assertIn("prodj: LIVE off, no pro-DJ app left", source)

    def test_a_kind_change_is_a_leave_and_a_join(self):
        """The client is named by its kind: kept, relay() would miss the
        sender and the silence would read the old kind."""
        source = _source_of("desk_said_hello")
        changed = source.index("if verdict == KIND_CHANGED:")
        self.assertLess(changed, source.index("if verdict != JOINED:"))
        branch = source[changed:source.index("if verdict != JOINED:")]
        self.assertIn("if desk_leaves(host):", branch)
        self.assertIn("live_off_when_no_prodj_is_left()", branch)
        self.assertIn("verdict = JOINED", branch)
        leaves = _source_of("desk_leaves")
        self.assertIn("_desk_clients.pop(host, None)", leaves)
        self.assertIn("drop_named(subscribers, client.name)", leaves)
        self.assertIn("desk_leaves(host)", _source_of("notice_desk_silence"))

    def test_announce_live_never_broadcasts(self):
        source = _source_of("announce_live")
        self.assertNotIn("broadcast(", source)
        self.assertNotIn("relay(", source)
        self.assertIn("_desk_clients", source)
        self.assertIn("osc_a3motion", source)
        self.assertIn("_stemdeck_client", source)

    def test_core_starts_with_it_off_and_says_nothing(self):
        self.assertIn("_prodj_live = ProdjLive()", CORE)


class Pointers(unittest.TestCase):
    def test_pointers_are_forwarded_only_through_the_gate(self):
        source = _source_of("osc_handler_pointer")
        self.assertIn("_prodj_live.admits(_said_kinds.gate_name(origin, client_address[0], client_address[1]), key)", source)
        self.assertIn("_stemdeck_client", source)
        self.assertIn("osc_a3motion", source)
        self.assertNotIn("relay(", source)
        self.assertNotIn("broadcast(", source)

    def test_the_stemdeck_family_handler_leaves_pointers_alone(self):
        self.assertIn("stemdeck.pointer", _source_of("osc_handler_stemdeck"))

    def test_the_handlers_are_mapped_from_the_truth(self):
        self.assertIn('dispatcher.map(_truth.address("prodj.live"), osc_handler_prodj_live', CORE)
        self.assertIn('("stemdeck.pointer", "motion.pointer")', CORE)
        self.assertIn("dispatcher.map(_truth.address(pointer), osc_handler_pointer", CORE)

    def test_values_refused_asks_the_live_gate(self):
        source = _source_of("values_refused")
        self.assertIn("who = origin_of(client_address)", source)
        self.assertIn("not _prodj_live.admits(_said_kinds.gate_name(who, host, port), key)", source)
        self.assertLess(source.index("_prodj_live.admits("),
                        source.index("relay_on_arrival("))
        self.assertEqual(source.count("origin_of(client_address)"), 1)



def _desk_word_handlers():
    """The handler of every family a desk word arrives in, read off the
    dispatcher's family table: a desk word is one with "desk" in `from` and
    Core in `to`, mapped by its first part."""
    addresses = json.loads(TRUTH.read_text())["addresses"]
    families = {entry["pattern"].split("/")[1] for entry in addresses.values()
                if "desk" in entry.get("from", []) and "core" in entry.get("to", [])}
    mapped = dict(re.findall(r'\("([a-z-]+)", (osc_handler_\w+)\)', CORE))
    return {family: mapped[family] for family in families if family in mapped}


class TheLiveGate(unittest.TestCase):
    def test_every_desk_word_family_handler_asks_the_gate(self):
        handlers = _desk_word_handlers()
        self.assertIn("aux-return", handlers)
        for family, handler in handlers.items():
            source = _source_of(handler)
            with self.subTest(family=family, handler=handler):
                self.assertIn("values_refused(client_address, address, osc_arguments)", source)
                self.assertLess(source.index("values_refused("), source.index("traffic.seen("))

    def test_every_desk_hello_leaves_its_kind_slot_or_not(self):
        source = _source_of("osc_handler_device_hello")
        said = source.index("_said_kinds.said(client_address[0], device)")
        self.assertLess(said, source.index("desk_said_hello("))
        self.assertIn("_said_kinds = SaidKinds(_truth.port(\"desk\", \"osc\"))", CORE)

    def test_the_pointer_gate_reads_the_kind_said_too(self):
        source = _source_of("osc_handler_pointer")
        self.assertIn("_said_kinds.gate_name(origin, client_address[0], client_address[1])", source)

    def test_a_refused_value_is_seen_as_an_arrival(self):
        source = _source_of("values_refused")
        gate = source.index("_prodj_live.admits(")
        refusal = source[gate:source.index("return True", gate)]
        self.assertIn("traffic.seen(IN, address,", refusal)


if __name__ == "__main__":
    unittest.main()
