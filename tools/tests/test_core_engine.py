# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Core's words to the a3-engine and the session that decides when to replay
them. Pure: no sockets. Channels in the arguments are 0-based indexes as in
Core's channel_infos; the addresses carry the 1-based wire number."""

import re
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
sys.path.insert(0, str(PACKAGE / "home/aaa/.local/lib"))

import a3_osc                                              # noqa: E402
from a3_core_decks import DeckSelector                     # noqa: E402
from a3_core_evening import replayable                     # noqa: E402
from a3_core_engine import (FORWARDED, EngineSession,      # noqa: E402
                            engine_channel_messages, filter_mode_message,
                            flag_message, forward_messages, open_message,
                            position_message, return_cue_message,
                            source_messages, state_messages, tempo_message)

TRUTH = a3_osc.load(PACKAGE / "usr/share/a3/a3-osc.json")


def selector_with(*sources):
    s = DeckSelector()
    s.from_evening({"channel.source": list(sources)})
    return s


def channels(n=4, **fields):
    return [SimpleNamespace(toggle_cue=False, toggle_fx=False, azimuth=None,
                            elevation=None, **fields) for _ in range(n)]


def master(mode=0, return_cue=False):
    return SimpleNamespace(fx_mode=SimpleNamespace(value=mode), return_cue=return_cue)


class Forwarding(unittest.TestCase):
    def test_forwarded_keys_keep_their_address_and_number(self):
        for key in sorted(FORWARDED):
            fields = {"ch": 2} if key.startswith("channel.") else {}
            self.assertEqual(forward_messages(TRUTH, key, fields, 0.25),
                             [(TRUTH.address(key, **fields), 0.25)], key)

    def test_unforwarded_keys_send_nothing(self):
        self.assertEqual(forward_messages(TRUTH, "channel.stem.turn", {"ch": 1}, 1), [])
        self.assertEqual(forward_messages(TRUTH, "channel.cue", {"ch": 1}, 1), [])

    def test_the_value_goes_as_a_float(self):
        ((_, value),) = forward_messages(TRUTH, "master.volume", {}, 1)
        self.assertIsInstance(value, float)


class Words(unittest.TestCase):
    def test_flags(self):
        self.assertEqual(flag_message(TRUTH, "cue", 0, True), ("/channel/1/cue", 1.0))
        self.assertEqual(flag_message(TRUTH, "filter", 3, False), ("/channel/4/filter", 0.0))

    def test_the_fx_flag_is_the_filter(self):
        self.assertEqual(flag_message(TRUTH, "fx", 1, True), ("/channel/2/filter", 1.0))

    def test_filter_mode_position_return_cue_tempo_open(self):
        self.assertEqual(filter_mode_message(TRUTH, 1), ("/filter/mode", 1.0))
        self.assertEqual(position_message(TRUTH, "azimuth", 2, 45), ("/channel/3/azimuth", 45.0))
        self.assertEqual(position_message(TRUTH, "elevation", 0, -9), ("/channel/1/elevation", -9.0))
        self.assertEqual(return_cue_message(TRUTH, True), ("/aux-return/cue", 1.0))
        self.assertEqual(return_cue_message(TRUTH, False), ("/aux-return/cue", 0.0))
        self.assertEqual(tempo_message(TRUTH, 124), ("/delay/bpm", 124.0))
        self.assertEqual(open_message(TRUTH), ("/engine/open", 1))


class Sources(unittest.TestCase):
    def test_sources_come_from_the_deck_selector(self):
        got = source_messages(TRUTH, selector_with(0, 2, 0, 1), 0)
        self.assertEqual(got[:4], [("/channel/1/source", 0), ("/channel/2/source", 2),
                                   ("/channel/3/source", 0), ("/channel/4/source", 1)])

    def test_return_source_names_a_deck(self):
        self.assertEqual(source_messages(TRUTH, DeckSelector(), 2)[-1],
                         ("/aux-return/source", 2))
        self.assertEqual(source_messages(TRUTH, DeckSelector(), 0)[-1],
                         ("/aux-return/source", 0))
        self.assertEqual(len(source_messages(TRUTH, DeckSelector(), 0)), 5)

    def test_deck_channels_send_no_positions_three_d_freq_q_to_the_engine(self):
        s = selector_with(0, 1, 0, 0)
        for key in ("channel.3d", "channel.filter.frequency", "channel.filter.q"):
            self.assertEqual(engine_channel_messages(TRUTH, s, key, {"ch": 2}, 0.5), [], key)
            self.assertEqual(len(engine_channel_messages(TRUTH, s, key, {"ch": 1}, 0.5)), 1, key)
        for key in ("channel.gain", "channel.volume", "channel.aux-send"):
            self.assertEqual(len(engine_channel_messages(TRUTH, s, key, {"ch": 2}, 0.5)), 1, key)

    def test_master_words_pass_through_the_channel_wrapper(self):
        self.assertEqual(engine_channel_messages(TRUTH, DeckSelector(), "master.booth", {}, 0.3),
                         [("/master/booth", 0.3)])


class State(unittest.TestCase):
    def test_state_carries_flags_mode_return_cue_sources_positions_and_tempo(self):
        chans = channels()
        chans[1].toggle_cue = True
        chans[2].toggle_fx = True
        chans[0].azimuth, chans[0].elevation = 30.0, 10.0
        got = state_messages(TRUTH, chans, master(mode=1, return_cue=True),
                             DeckSelector(), 0, 126.0)
        for message in (("/channel/2/cue", 1.0), ("/channel/1/cue", 0.0),
                        ("/channel/3/filter", 1.0), ("/filter/mode", 1.0),
                        ("/aux-return/cue", 1.0), ("/channel/1/source", 0),
                        ("/aux-return/source", 0), ("/channel/1/azimuth", 30.0),
                        ("/channel/1/elevation", 10.0), ("/delay/bpm", 126.0)):
            self.assertIn(message, got)

    def test_unknown_position_and_tempo_are_left_unsaid(self):
        got = state_messages(TRUTH, channels(), master(), DeckSelector(), 0, None)
        self.assertFalse([m for m in got if "azimuth" in m[0] or "elevation" in m[0]])
        self.assertFalse([m for m in got if m[0] == "/delay/bpm"])

    def test_deck_channels_have_no_positions_in_the_state(self):
        chans = channels()
        for c in chans:
            c.azimuth = c.elevation = 5.0
        got = state_messages(TRUTH, chans, master(), selector_with(0, 1, 0, 0), 0, None)
        self.assertFalse([m for m in got if m[0].startswith("/channel/2/") and
                          ("azimuth" in m[0] or "elevation" in m[0])])
        self.assertIn(("/channel/1/azimuth", 5.0), got)


class Words_in_the_truth(unittest.TestCase):
    def test_every_word_is_in_the_truth(self):
        chans = channels()
        for c in chans:
            c.azimuth = c.elevation = 1.0
        words = state_messages(TRUTH, chans, master(), DeckSelector(), 1, 120.0)
        words += [open_message(TRUTH)]
        for address, _ in words:
            self.assertIsNotNone(TRUTH.match(address), address)

    def _carried_keys(self):
        carries = TRUTH.listener("engine", "osc")["carries"]
        carries = carries.split("--", 1)[1]
        keys = set()
        for key in TRUTH.addresses():
            tail = key.split(".", 1)[1] if key.startswith(("channel.", "master.")) else key
            keys.add((key, tail))
        found = set()
        for token in re.split(r"[,/]\s*", carries):
            token = token.strip()
            if not token:
                continue
            for key, tail in keys:
                if token == key or token == tail or token.rstrip(".*") == key:
                    found.add(key)
        return found

    def test_every_engine_input_is_replayed_or_in_the_state(self):
        chans = channels()
        for c in chans:
            c.azimuth = c.elevation = 1.0
        state = {m[0] for m in state_messages(TRUTH, chans, master(), DeckSelector(), 1, 120.0)}
        state = {TRUTH.match(a)[0] for a in state}
        replayed = set()
        values = {TRUTH.address(k, **({"ch": 1} if k.startswith("channel.") else {})): 0.5
                  for k, e in TRUTH.addresses().items() if e.get("relayed_to")}
        for address, _ in replayable(TRUTH, {"values": values}):
            replayed.add(TRUTH.match(address)[0])
        carried = self._carried_keys()
        self.assertTrue(carried >= {"channel.cue", "channel.source", "delay.bpm"}, carried)
        for key in carried - {"engine.open"}:
            # channel.3d is Motion's live word, never replayed or stated.
            if key == "channel.3d":
                continue
            self.assertTrue(key in replayed or key in state, key)


class Session(unittest.TestCase):
    def test_a_new_boot_id_asks_for_a_replay_a_known_one_does_not(self):
        s = EngineSession([], lambda: [])
        self.assertTrue(s.heard_hello("a"))
        self.assertEqual(s.round, 1)
        self.assertFalse(s.heard_hello("a"))
        self.assertEqual(s.round, 1)
        self.assertTrue(s.heard_hello("b"))
        self.assertEqual(s.round, 2)

    def test_core_restart_replays_into_an_open_engine(self):
        # A fresh session has heard nobody: the first hello, whatever its id,
        # is a replay.
        for boot_id in ("x", 0, 12345):
            self.assertTrue(EngineSession([], lambda: []).heard_hello(boot_id))

    def test_evening_is_the_files_until_the_first_replay_then_core_s(self):
        s = EngineSession(["file"], lambda: ["live"])
        self.assertFalse(s.live)
        self.assertEqual(s.evening(), ["file"])
        s.heard_hello("a")
        self.assertEqual(s.evening(), ["file"])
        self.assertTrue(s.replayed(s.round))
        self.assertTrue(s.live)
        self.assertEqual(s.evening(), ["live"])

    def test_a_replay_overtaken_by_a_new_hello_does_not_count(self):
        s = EngineSession(["file"], lambda: ["live"])
        s.heard_hello("a")
        started = s.round
        s.heard_hello("b")
        self.assertFalse(s.replayed(started))
        self.assertFalse(s.live)
        self.assertEqual(s.evening(), ["file"])



class TheReturnSourceFollowsTheDesksMode(unittest.TestCase):
    """Engine mode keeps the desk's return word: STEM mode (the desk's word 1)
    plays the delay (R1), ANALOG plays analog 11/12."""

    def test_stem_mode_returns_the_delay(self):
        from a3_core_engine import RETURN_DELAY, engine_return_source
        from a3_core_stems import STEM_MODE
        self.assertEqual(RETURN_DELAY, 1)
        self.assertEqual(engine_return_source(STEM_MODE), RETURN_DELAY)

    def test_analog_mode_returns_analog(self):
        from a3_core_engine import engine_return_source
        from a3_core_stems import ANALOG_MODE
        self.assertEqual(engine_return_source(ANALOG_MODE), 0)


if __name__ == "__main__":
    unittest.main()
