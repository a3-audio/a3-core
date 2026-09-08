"""What the loader has to get right.

Standard library only: a3-core has no test setup, pythonosc is on no machine
here but the Core itself, and importing a3-core.py opens sockets and starts a
server. Run with `python3 -m unittest discover tools/tests`.
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]
                       / "platform-config/debian-x86_64/a3-core"
                       / "home/aaa/.local/lib"))

from a3_core_layout import Layout, load_layout, LayoutError   # noqa: E402


def written(text):
    """A layout file holding `text`, cleaned up with the test."""
    handle = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
    handle.write(text)
    handle.close()
    return Path(handle.name)


MINIMAL = json.dumps({
    "channels": [
        {"track_input": 12, "track_multi_enc": 11, "track_stereo_enc": 10,
         "track_channelbus": 9, "track_pfl": 4, "enc_main_azimuth": 8,
         "enc_main_elevation": 9, "enc_phones_solo": 12}
    ],
    # Master is required: defaulting those would drive tracks 0 and 0, on the
    # master bus of all places.
    "master": {"track_masterbus": 1, "track_booth": 2, "track_phones": 3,
               "track_ph_mix": 8, "aux_return": 25},
    "addresses": {
        "reaper_track_volume": "/track/{track}/volume",
        "led_pfl": "/channel/{channel}/led/pfl",
    },
})


class TrackMap(unittest.TestCase):
    def test_a_channel_carries_its_reaper_tracks(self):
        layout = load_layout(written(MINIMAL))
        self.assertEqual(layout.channel(0).track_input, 12)
        self.assertEqual(layout.channel(0).track_pfl, 4)

    def test_a_channel_that_is_not_there_is_an_error_not_a_zero(self):
        # A missing track number would read as track 0 and quietly drive
        # whatever REAPER has there. Better to refuse to start.
        layout = load_layout(written(MINIMAL))
        with self.assertRaises(LayoutError):
            layout.channel(3)

    def test_a_channel_missing_a_track_is_refused(self):
        broken = json.dumps({
            "channels": [{"track_input": 12}],
            "master": {"track_masterbus": 1, "track_booth": 2,
                       "track_phones": 3, "track_ph_mix": 8,
                       "aux_return": 25},
            "addresses": {}})
        with self.assertRaises(LayoutError):
            load_layout(written(broken))


class Addresses(unittest.TestCase):
    def test_an_address_is_filled_in_by_name(self):
        layout = load_layout(written(MINIMAL))
        self.assertEqual(layout.address("reaper_track_volume", track=11),
                         "/track/11/volume")

    def test_an_address_nobody_named_is_an_error(self):
        layout = load_layout(written(MINIMAL))
        with self.assertRaises(LayoutError):
            layout.address("no_such_address")

    def test_a_placeholder_left_unfilled_is_an_error(self):
        # Sending "/track/{track}/volume" literally is a message REAPER
        # silently ignores -- the loudest such failure is no failure at all.
        layout = load_layout(written(MINIMAL))
        with self.assertRaises(LayoutError):
            layout.address("reaper_track_volume")


class BadFiles(unittest.TestCase):
    def test_a_file_that_is_not_there_is_refused(self):
        with self.assertRaises(LayoutError):
            load_layout(Path("/nonexistent/layout.json"))

    def test_a_file_that_is_not_json_is_refused(self):
        with self.assertRaises(LayoutError):
            load_layout(written("{not json"))


if __name__ == "__main__":
    unittest.main()


FULL = json.dumps({
    "channels": [
        {"track_input": 12, "track_multi_enc": 11, "track_stereo_enc": 10,
         "track_channelbus": 9, "track_pfl": 4, "enc_main_azimuth": 8,
         "enc_main_elevation": 9, "enc_phones_solo": 12}
    ],
    "master": {"track_masterbus": 1, "track_booth": 2, "track_phones": 3,
               "track_ph_mix": 8, "aux_return": 25},
    "fx_slots": {"gain": 1, "eq": 2, "hipass": 3, "lopass": 4},
    "gain_params": {"channelbus": [1, 15], "masterbus": [1, 15, 29]},
    "addresses": {},
})


class TheRestOfTheMap(unittest.TestCase):
    """Not everything in the REAPER project belongs to a channel."""

    def setUp(self):
        self.layout = load_layout(written(FULL))

    def test_the_master_tracks_are_there(self):
        self.assertEqual(self.layout.master.track_masterbus, 1)
        self.assertEqual(self.layout.master.aux_return, 25)

    def test_an_fx_slot_is_named_not_numbered_at_the_call_site(self):
        # FX_INDEX_EQ = 2 said what slot the EQ is in and nothing about why.
        self.assertEqual(self.layout.fx_slot("eq"), 2)

    def test_an_fx_slot_nobody_named_is_an_error(self):
        with self.assertRaises(LayoutError):
            self.layout.fx_slot("reverb")

    def test_the_gain_parameter_lists_come_through(self):
        # A gain plugin has its value on several parameters at once; the list
        # is which. Written out at four call sites before this.
        self.assertEqual(self.layout.gain_params("channelbus"), [1, 15])

    def test_a_gain_list_nobody_named_is_an_error(self):
        with self.assertRaises(LayoutError):
            self.layout.gain_params("nothing")

    def test_a_missing_master_block_is_refused(self):
        # Defaulting these would drive tracks 0 and 0 -- silently, and on the
        # master bus of all places.
        with self.assertRaises(LayoutError):
            load_layout(written(json.dumps({"channels": [], "addresses": {}})))
