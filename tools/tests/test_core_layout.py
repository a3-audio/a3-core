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


#: Das Layout, das ausgeliefert wird -- fuer die Faelle, in denen die Frage
#: lautet "steht es dort wirklich drin" und nicht "haelt der Leser sich an
#: seine Regeln".
SHIPPED = (Path(__file__).resolve().parents[2]
           / "platform-config/debian-x86_64/a3-core/home/aaa/.local"
           / "share/a3-core/layout.json")

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


SHAPES = json.dumps({
    "channels": [],
    "master": {"track_masterbus": 1, "track_booth": 2, "track_phones": 3,
               "track_ph_mix": 8, "aux_return": 25},
    "addresses": {
        "fx_param": "/track/{track}/fx/{slot}/fxparam/{param}/value",
        "track_volume": "/track/{track}/volume",
        "track_mute": "/track/{track}/mute",
        "led_pfl": "/channel/{channel}/led/pfl",
        "fx_mode_led": "/fx/led",
    },
    "fx_params": {
        "elevation": 8,
        "eq_high": 1, "eq_mid": 2, "eq_low": 3,
        "filter_frequency": 7, "filter_resonance": 6,
    },
})


class AddressShapes(unittest.TestCase):
    """Twenty-seven addresses turned out to be five shapes used twenty-seven
    times. What differed between them was not the address but the VST
    parameter number, written out at the call site as 1, 6, 7, 8 or 15."""

    def setUp(self):
        self.layout = load_layout(written(SHAPES))

    def test_the_fx_parameter_shape_covers_most_of_them(self):
        self.assertEqual(
            self.layout.address("fx_param", track=10, slot=4, param=8),
            "/track/10/fx/4/fxparam/8/value")

    def test_a_parameter_number_is_named(self):
        # fxparam/8 said where the elevation sits and nothing about what it
        # is. Reading the project to find out is what this replaces.
        self.assertEqual(self.layout.fx_param("elevation"), 8)
        self.assertEqual(self.layout.fx_param("filter_resonance"), 6)

    def test_a_parameter_nobody_named_is_an_error(self):
        with self.assertRaises(LayoutError):
            self.layout.fx_param("reverb_size")

    def test_an_address_without_placeholders_needs_none(self):
        self.assertEqual(self.layout.address("fx_mode_led"), "/fx/led")

    def test_each_led_is_its_own_address(self):
        # Named one each rather than one shape with a word in it. The three
        # LEDs are three things a channel says, not three values of one -- and
        # a shape whose last segment is a placeholder cannot be held against
        # the source, where the word is written out.
        self.assertEqual(self.layout.address("led_pfl", channel=2),
                         "/channel/2/led/pfl")


class TheMapRunsBothWays(unittest.TestCase):
    """Which A3 channel a REAPER track belongs to.

    The map has always been read one way -- channel to track, because that is
    the direction messages travel. REAPER's feedback travels the other way and
    arrives naming a track, so the same file has to answer both.

    Derived rather than written down twice: a second list would be a second
    thing to keep in step, and the way that goes wrong is silent -- a value
    landing on the wrong channel's knob.
    """

    def setUp(self):
        self.layout = load_layout(written(json.dumps({
            "channels": [
                {"track_input": 12, "track_multi_enc": 11,
                 "track_stereo_enc": 10, "track_channelbus": 9,
                 "track_pfl": 4, "enc_main_azimuth": 8,
                 "enc_main_elevation": 9, "enc_phones_solo": 12},
                {"track_input": 16, "track_multi_enc": 15,
                 "track_stereo_enc": 14, "track_channelbus": 13,
                 "track_pfl": 5, "enc_main_azimuth": 13,
                 "enc_main_elevation": 14, "enc_phones_solo": 17},
            ],
            "master": {"track_masterbus": 1, "track_booth": 2,
                       "track_phones": 3, "track_ph_mix": 8, "aux_return": 25},
            "addresses": {},
        })))

    def test_a_track_says_which_channel_it_is(self):
        self.assertEqual(self.layout.channel_for_track(12), 0)
        self.assertEqual(self.layout.channel_for_track(13), 1)

    def test_it_says_which_of_the_channels_tracks_it_is(self):
        # Track 12 is channel 0's input, 9 is its channelbus. Knowing which
        # one turns a value into a parameter rather than only a channel.
        self.assertEqual(self.layout.track_role(12), (0, "track_input"))
        self.assertEqual(self.layout.track_role(9), (0, "track_channelbus"))
        self.assertEqual(self.layout.track_role(14), (1, "track_stereo_enc"))

    def test_a_track_that_belongs_to_no_channel_says_so(self):
        self.assertIsNone(self.layout.channel_for_track(1))
        self.assertIsNone(self.layout.track_role(99))

    def test_the_encoder_numbers_are_not_tracks(self):
        # enc_main_azimuth=8 is a different numbering and must not be answered
        # as a track -- 8 is the master's ph_mix, which is somebody else's.
        self.assertIsNone(self.layout.channel_for_track(8))


SENDS = json.dumps({
    "channels": [{"track_input": 12, "track_multi_enc": 11,
                  "track_stereo_enc": 10, "track_channelbus": 9,
                  "track_pfl": 4, "enc_main_azimuth": 8,
                  "enc_main_elevation": 9, "enc_phones_solo": 12}],
    "master": {"track_masterbus": 1, "track_booth": 2, "track_phones": 3,
               "track_ph_mix": 8, "aux_return": 25},
    "sends": {"fx": 3},
    "addresses": {"track_send": "/track/{track}/send/{send}/volume"},
})


class TheSends(unittest.TestCase):
    """A send is not an FX slot, and giving it its own door says so.

    The number was about to be a bare 3 in a3-core.py -- the same shape of
    mistake as the bare 2 the encoder pots sat on until 2026-09-12, where the
    layout knew everything about the track and nothing about which plugin on
    it. A send that moves in the REAPER project has to be findable by reading
    the layout, not by grepping f-strings.
    """

    def test_a_send_is_named_rather_than_numbered(self):
        self.assertEqual(load_layout(written(SENDS)).send("fx"), 3)

    def test_a_send_nobody_has_is_refused(self):
        """Loudly, like fx_slot. A send resolving to None would address
        /track/9/send/None/volume, and REAPER would ignore that in silence."""
        with self.assertRaises(LayoutError):
            load_layout(written(SENDS)).send("reverb")

    def test_a_layout_without_sends_refuses_every_name(self):
        with self.assertRaises(LayoutError):
            load_layout(written(MINIMAL)).send("fx")

    def test_the_address_is_built_by_the_layout(self):
        self.assertEqual(
            load_layout(written(SENDS)).address("track_send", track=9, send=3),
            "/track/9/send/3/volume")


class TheEncoderGainsAreNamedLikeEveryOtherGain(unittest.TestCase):
    """Die Überblendung schrieb ihre Parameter als feste Zahlen.

    `apply_3d_crossfade()` schickte `fx/1/fxparam/1` und `fxparam/15` an den
    Stereo-Encoder und `fxparam/1` an den Multi-Encoder — die einzigen fest
    verdrahteten Verstärkungsparameter im ganzen Programm, während jeder
    andere Bus seine Liste aus `gain_params` liest. Beim nächsten Umbau des
    REAPER-Projekts wären sie stillschweigend falsch geworden, und zwar an
    einer Stelle, die man hört.

    Die Namen sind überholt: der IEM StereoEncoder liegt nicht mehr auf
    `1-stereo-enc`, und beide Wege gehen in denselben MultiEncoder — der
    bewegte auf dessen Kanäle 1–4, der stehende auf 5–n. `stereo_enc` müsste
    `steady` heißen; umbenannt ist es noch nicht, weil es layout.json,
    a3_core_layout.py, a3-core.py und die OSC-Doku zugleich berührt.

    **Die Längen sind eine Entscheidung, keine Momentaufnahme.**
    `1-stereo-enc` trägt vier Airwindows-Instanzen, gefahren werden zwei
    (`fxparam/1` und `/15`). Vom Maintainer am 2026-09-21 entschieden: *„die
    airwindow container haben immer funktioniert. bitte nicht ändern,
    vermutlich sind es die isolator3 plugins."* Die übrigen zwei sind die
    Isolator 3, mit denen das gefilterte Band herausgezogen wird — keine
    Verstärkungen.

    Deshalb prüft das hier auf **genau** `[1, 15]` und `[1]` und nicht bloß
    darauf, dass eine Liste existiert: die Art, wie das kaputtgeht, ist
    jemand, der die Liste „vervollständigt" und damit die Blende über den
    Filter legt. Siehe
    issues/a3-core-crossfade-schreibt-zwei-von-vier-verstaerkungen.md.
    """

    def test_both_encoders_have_a_gain_list(self):
        layout = load_layout(SHIPPED)
        self.assertEqual([1, 15], layout.gain_params("stereo_enc"))
        self.assertEqual([1], layout.gain_params("multi_enc"))

    def test_the_slot_they_sit_in_is_named_too(self):
        # fx/1 stand als Zahl im f-String, obwohl es den Namen längst gab.
        self.assertEqual(1, load_layout(SHIPPED).fx_slot("enc"))
