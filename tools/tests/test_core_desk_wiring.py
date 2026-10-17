# SPDX-FileCopyrightText: 2026 Patric Schmitz, Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Core follows the virtual desks (spec virtual-desk). The decisions are in
test_core_desks; this holds that a3-core.py asks them and acts on the answer.
Read off the source: Core itself cannot be started in a test."""

import ast
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/bin/a3-core.py").read_text()


def _source_of(function_name):
    node = next(n for n in ast.walk(ast.parse(CORE))
                if isinstance(n, ast.FunctionDef) and n.name == function_name)
    return ast.get_source_segment(CORE, node)


class AHelloFromADesk(unittest.TestCase):
    def test_is_followed_only_with_cores_own_truth(self):
        source = _source_of("osc_handler_device_hello")
        self.assertIn('device == "desk" and their_hash == _devices.own_hash', source)
        self.assertIn("desk_said_hello(", source)

    def test_a_joining_desk_becomes_a_subscriber_and_hears_everything(self):
        source = _source_of("desk_said_hello")
        self.assertIn("_desks.hello(", source)
        self.assertIn("add_subscriber(subscribers,", source)
        self.assertIn("say_the_whole_state(", source)

    def test_a_fifth_desk_is_said_in_the_journal(self):
        self.assertIn("REFUSED_FIRST", _source_of("desk_said_hello"))

    def test_the_window_names_each_desk_by_its_host(self):
        self.assertIn("desk_name(", _source_of("osc_handler_device_hello"))


class Silence(unittest.TestCase):
    def test_is_watched_on_every_tick(self):
        self.assertIn("notice_desk_silence(", _source_of("on_tick"))

    def test_a_silent_desk_leaves_the_subscribers(self):
        source = _source_of("notice_desk_silence")
        self.assertIn("_desks.silence(", source)
        self.assertIn("drop_named(subscribers,", source)


class WhoSaidIt(unittest.TestCase):
    """Review focus 1: a desk and a Motion on one host."""

    def test_a_desk_is_known_by_its_socket_before_its_host(self):
        source = _source_of("origin_of")
        self.assertIn("_desks.name_of(", source)
        self.assertIn("peer_name(", source)

    def test_channel_origin_comes_from_origin_of(self):
        for handler in ("osc_handler_channel", "osc_handler_master",
                        "osc_handler_aux_return", "osc_handler_filter"):
            source = _source_of(handler)
            self.assertIn("origin = origin_of(client_address)", source, handler)


class BeatAndMeters(unittest.TestCase):
    def test_every_desk_gets_the_beat(self):
        self.assertIn("_desk_clients", _source_of("osc_handler_beat"))

    def test_the_relay_feeds_the_motion_and_every_desk(self):
        source = _source_of("vu_destinations")
        self.assertIn("_motion.vu_destination()", source)
        self.assertIn("_desks.vu_destinations()", source)
        self.assertIn("args=(vu_inbox, vu_destinations)", CORE)


class AnotherThreadReadsTheList(unittest.TestCase):
    def test_broadcast_walks_a_snapshot(self):
        """REAPER's feedback thread broadcasts while a desk joins or leaves
        on the serve thread; a list changed under a running loop can skip
        a screen."""
        self.assertIn("for client in tuple(subscribers):", _source_of("broadcast"))


class OnlyTheServeThreadChangesTheList(unittest.TestCase):
    """A desk joins and leaves on the serve thread, enforced by refusal."""

    def test_joining_checks_the_owner_before_adding(self):
        source = _source_of("desk_said_hello")
        self.assertIn('_subscribers_owner.check("a desk joining")', source)
        self.assertLess(source.index("_subscribers_owner.check("),
                        source.index("add_subscriber("))

    def test_leaving_checks_the_owner_before_dropping(self):
        source = _source_of("notice_desk_silence")
        self.assertIn('_subscribers_owner.check("a desk leaving")', source)
        self.assertLess(source.index("_subscribers_owner.check("),
                        source.index("drop_named("))

    def test_the_serve_thread_claims_the_list_just_before_serving(self):
        main = CORE[CORE.index('if __name__ == "__main__":'):]
        self.assertIn("_subscribers_owner.claim()", main)
        self.assertLess(main.index("_subscribers_owner.claim()"),
                        main.index("serve(server.socket"))

    def test_the_owner_lives_beside_the_desks(self):
        self.assertIn("_subscribers_owner = OwningThread()", CORE)


class StillParses(unittest.TestCase):
    def test_core_parses(self):
        ast.parse(CORE)


if __name__ == "__main__":
    unittest.main()
