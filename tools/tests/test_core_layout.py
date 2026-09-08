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
        broken = json.dumps({"channels": [{"track_input": 12}],
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
