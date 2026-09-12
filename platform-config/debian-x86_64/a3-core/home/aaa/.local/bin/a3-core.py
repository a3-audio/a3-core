# A³ Core is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# A³ Core is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with A³ Core.  If not, see <https://www.gnu.org/licenses/>.

# © Copyright 2021 Raphael Eismann, Patric Schmitz

"""
This version is a static hardcoded version of osc-router. It will
transform into more dynamic code with external configfiles for osc in-
and output mappings.  For now it takes OSC adresses, interpolates
values and sends them to destinations.
"""

import argparse
import json
import os
import signal
import sys
import threading
from pathlib import Path
import numpy as np
import time
#import rtmidi
import math
from typing import List, Any, Optional, Tuple
from enum import Enum
from dataclasses import dataclass
from pythonosc import dispatcher  # type: ignore
from pythonosc import dispatcher as osc_dispatcher  # type: ignore
from pythonosc import osc_server

# The layout lives beside this script in the package, not on sys.path. Added
# explicitly so a3-core.py can be started from anywhere -- systemd does not
# promise a working directory.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "lib"))
from a3_core_layout import load_layout   # noqa: E402
from a3_core_curves import CurveNotInvertible, invert, load_curves  # noqa: E402
from a3_core_crossfade import crossfade_gains   # noqa: E402
from a3_core_buttons import (NO_CHANGE, wanted_fx_mode,   # noqa: E402
                             wanted_toggle)   # noqa: E402
from a3_core_echo import EchoFilter   # noqa: E402
from a3_core_reverse import reverse_for   # noqa: E402
from a3_core_state import StateFile, apply_state, state_of   # noqa: E402
from a3_core_recall import (FX_MODE_WORDS, Relayed, led_message,   # noqa: E402
                            recall_messages)   # noqa: E402
from a3_core_traffic import (ANSWERERS, COMMANDERS, IN,   # noqa: E402
                             OUT, Traffic, peer_name)   # noqa: E402
from a3_core_web import start_window, window_address   # noqa: E402

LAYOUT_PATH = (Path(__file__).resolve().parent.parent
               / "share/a3-core/layout.json")

#: Cores eigener Stand. Under XDG's state directory rather than beside the
#: layout: the layout describes the rig and ships with the package, this is
#: what the evening did to it and must survive an update untouched.
STATE_PATH = (Path(os.environ.get("XDG_STATE_HOME",
                                  Path.home() / ".local/state"))
              / "a3-core/state.json")

#: The curves as they were recorded, which is what makes them invertible.
CURVES_PATH = (Path(__file__).resolve().parent.parent
               / "share/a3-core/curves-golden.json")

# Read once, at the top, because everything below is built out of it -- the FX
# slot numbers, the master tracks and the per-channel ones. A layout that
# cannot be read raises here and Core does not come up, which is the right
# failure: coming up against the wrong tracks is worse than not coming up.
_layout = load_layout(LAYOUT_PATH)
from pythonosc.udp_client import SimpleUDPClient  # type: ignore

OSC_PORT_CORE: int = 9000

# Which FX slot on a track holds which plugin. Out of the layout rather than
# written here: a slot that moves in the REAPER project is a number to change
# in one place, and the name says which plugin rather than only where it sits.
FX_INDEX_GAIN: int = _layout.fx_slot("gain")
FX_INDEX_EQ: int = _layout.fx_slot("eq")
FX_INDEX_EQ_ENC: int = _layout.fx_slot("eq_enc")
FX_INDEX_HIPASS: int = _layout.fx_slot("hipass")
FX_INDEX_LOPASS: int = _layout.fx_slot("lopass")
FX_INDEX_CHANNEL_VOLUME: int = _layout.fx_slot("channel_volume")
FX_INDEX_STEREO_ENC: int = _layout.fx_slot("stereo_enc")
FX_INDEX_ENC: int = _layout.fx_slot("enc")
#: The plugin on the stereo-encoder track that carries the encoder's two
#: pots. It was the literal 2 in both pot branches -- the one number in the
#: forward path that the layout did not name, which is also why the reverse
#: table could not name it either.
FX_INDEX_ENC_POTS: int = _layout.fx_slot("enc_pots")

CHANNEL_ENC_MAIN: int = 26
CHANNEL_ENC_PHONES: int = 27
CHANNEL_ENC_DELAY: int = 25

# OSC clients
# Where the three destinations live. Named here rather than only inline so
# all of them -- not just the two on the rig -- can be pointed at a listener
# on a bench: without that the only way to see what Core sends is to stand in
# front of the rig, and a path nobody can watch is a path nobody can test.
A3MIXER_HOST, A3MIXER_PORT = '192.168.43.55', 7771
A3MOTION_HOST, A3MOTION_PORT = '192.168.43.54', 8700
REAPER_HOST, REAPER_PORT = '127.0.0.1', 9001

# What Core sends, remembered so the echo can be told from news.
#
# Wrapped rather than recorded at each of the thirty call sites: one place
# that cannot be forgotten when a thirty-first is added.
#
# Every WatchedClient feeds this now -- mixer, motion and the three IEM
# clients besides REAPER, where before only REAPER's was wrapped. Nothing
# reads it back except REAPER's own feedback port, so there is no live
# collision today. But is_echo() does `abs(expected - value)`, and /fx/led
# is stored with a string value -- so the day two destinations ever share an
# address, this becomes a TypeError in the feedback thread rather than a
# silently wrong comparison.
echo_filter = EchoFilter()

#: Every message Core has seen, for the window to read. One instance, at
#: module scope, because the handlers are module-level functions and there is
#: no object here to hang it on.
traffic = Traffic()

#: Which host is which device, for naming an incoming message's sender.
#: Rebuilt from the arguments below, since --mixer, --motion and --reaper can
#: each be pointed somewhere else for a bench run. All three live in one map;
#: which of them could have sent a given message is decided per tap, from the
#: port it arrived at -- see peer_name's `only`.
PEER_HOSTS = {"mixer": A3MIXER_HOST, "motion": A3MOTION_HOST,
              "reaper": REAPER_HOST}

#: Whether to also print every message. See --print-osc.
_print_osc = False


class WatchedClient:
    """A client that remembers what it sent, and knows who it is.

    The name is here so that one wrapper can say which destination a message
    went to without the call sites repeating it. Every outgoing client is one
    of these -- two kinds of client, where the caller has to know which one it
    is holding, is a trap; and a tap that saw only some of them would answer
    the question "is anything reaching the mixer?" with a confident no.
    """

    def __init__(self, client, name):
        self._client = client
        self.name = name

    def send_message(self, address, value):
        echo_filter.sent(address, value)
        traffic.seen(OUT, address, value, self.name)
        self._client.send_message(address, value)


osc_a3mixer = WatchedClient(SimpleUDPClient(A3MIXER_HOST, A3MIXER_PORT),
                            "mixer")
osc_a3motion = WatchedClient(SimpleUDPClient(A3MOTION_HOST, A3MOTION_PORT),
                             "motion")
osc_reaper = WatchedClient(SimpleUDPClient(REAPER_HOST, REAPER_PORT),
                           "reaper")

udp_clients_iem = tuple(
    WatchedClient(SimpleUDPClient('127.0.0.1', 1337 + index), "iem")
    for index in range(3))

@dataclass
class MasterInfo:
    # The track numbers come from the layout, the same way the channels' do;
    # what is left here is the one thing that changes while it runs.
    track_masterbus: int
    track_booth: int
    track_phones: int
    track_ph_mix: int
    aux_return: int

    class FXMode(Enum):
        LOW_PASS = 0
        HIGH_PASS = 1
    fx_mode: FXMode = FXMode.LOW_PASS

master_info = MasterInfo(
    track_masterbus=_layout.master.track_masterbus,
    track_booth=_layout.master.track_booth,
    track_phones=_layout.master.track_phones,
    track_ph_mix=_layout.master.track_ph_mix,
    aux_return=_layout.master.aux_return,
)

@dataclass
class ChannelInfo:
    enc_main_azimuth: int
    enc_main_elevation: int
    enc_phones_solo: int
    track_input: int
    track_channelbus: int
    track_pfl: int
    track_multi_enc: int
    track_stereo_enc: int

    toggle_fx: bool = False
    toggle_pfl: bool = False
    toggle_3d: bool = False

    # Where the sound is, as Motion last said it. Held here because nobody
    # else can be asked: Core writes the position straight to the IEM
    # plugins' own OSC port, not through a REAPER track, so REAPER never
    # reports it back and there is no feedback path to read it from. The
    # plugins do hold it -- their receiver sets the host parameter, so the
    # project saves it -- but they are written to, not read from.
    #
    # None is not 0.0. Zero degrees is the front of the room, a real
    # position, so a default of 0.0 would be Core claiming to know where a
    # channel sits before it has ever heard. A recall leaves an unknown
    # position unsaid instead. See a3_core_recall.position_messages and
    # issues/a3-core-position-hat-keinen-rueckweg-und-keinen-halter.md.
    azimuth: Optional[float] = None
    elevation: Optional[float] = None

    # How far this channel is spread into the 3D field, as Motion last sent
    # it. Held for a different reason than the position: this one *does*
    # reach REAPER, but as two gains on two tracks, and a single number
    # cannot say which input produced them -- so REAPER cannot report it
    # back. Unlike the position it survives a restart, because a knob changes
    # at the rate of a decision rather than continuously. See
    # a3_core_recall.REMEMBERED_CONTROLS and a3_core_state.CHANNEL_FIELDS.
    three_d: Optional[float] = None

    # Still the dead half of the old elevation/width cache: width was meant
    # to be narrowed towards the zenith, nothing assigns it, and
    # send_elevation() -- which reads elevation and would now have to cope
    # with None -- is still never called.
    # See issues/a3-core-elevation-cache-ist-tot.md.
    width: float = 0.0

# Built from the layout file rather than written out here.
#
# The track numbers are the map between an A3 channel and the REAPER project:
# change the project and they have to follow, and as a literal nothing about
# them said so. They are data now, beside the REAPER project they must agree
# with -- see .local/share/a3-core/layout.json.
#
# What stays in the dataclass is what changes while the thing runs: toggle_3d,
# toggle_fx and toggle_pfl. A number that describes the rig and a flag that
# describes the moment are two different kinds of thing, and only one of them
# belongs in a file that ships.
#
# `azimuth` and `elevation` belong to that second kind and are assigned in
# osc_handler_channel, where the position is passed on to the IEM plugins.
# `width` does not: nothing assigns it, and send_elevation() -- the one reader
# of either -- is still never called.
# See issues/a3-core-elevation-cache-ist-tot.md.
channel_infos = tuple(
    ChannelInfo(
        enc_main_azimuth=_layout.channel(index).enc_main_azimuth,
        enc_main_elevation=_layout.channel(index).enc_main_elevation,
        enc_phones_solo=_layout.channel(index).enc_phones_solo,
        track_input=_layout.channel(index).track_input,
        track_channelbus=_layout.channel(index).track_channelbus,
        track_pfl=_layout.channel(index).track_pfl,
        track_multi_enc=_layout.channel(index).track_multi_enc,
        track_stereo_enc=_layout.channel(index).track_stereo_enc,
    )
    for index in range(_layout.channel_count)
)

#: What the evening did to the rig, as far as Core alone knows it.
#:
#: Only what has no REAPER parameter behind it is in here -- the three toggles
#: a channel carries, the filter mode, and the cached elevation and width.
#: Everything continuous is REAPER's and comes back from REAPER; see
#: a3_core_reverse.
_state_file = StateFile(STATE_PATH)
apply_state(_state_file.load(), channel_infos, master_info)


#: What Core has passed on, so it can say it again. In memory only -- see
#: a3_core_recall for why this is not a file.
_relayed = Relayed()


def client_for(device):
    """The client a device name stands for.

    Looked up rather than held, because --mixer and --motion rebind these at
    module scope after this module has been read.
    """
    return osc_a3mixer if device == "mixer" else osc_a3motion


def remember_state():
    """Note the state after handling a message.

    Called from the two handlers rather than from the six places a flag is
    flipped: a handler is where a message is finished with, and six call sites
    are six chances for the seventh to be forgotten. It costs nothing to
    offer a state that has not changed -- StateFile compares before it starts
    its clock.
    """
    _state_file.remember(state_of(channel_infos, master_info))


def send_crossfade(channel_index, value):
    """Put a channel where the control says, between its two encoders.

    Called from both roads -- `/channel/n/fx-send`, the A3 Mixer's pot, and
    `/channel/n/3d`, A3 Motion's. They mean the same thing and have to stay
    that way until the mixer's knob is given its own job back; see
    issues/a3-core-fx-send-fuehrt-noch-die-3d-funktion.md.

    One function rather than the two identical blocks this was, because both
    roads now also have to write the value into Core's own memory. Nothing
    fails if one of them forgets: Core simply answers the next recall with a
    value from before somebody turned that pot, and the room hears the sound
    snap back. The arithmetic itself is in a3_core_crossfade, where a test
    can reach it.
    """
    channel_infos[channel_index].three_d = float(value)

    stereo_gain, multi_gain = crossfade_gains(value)
    track_stereo_enc = channel_infos[channel_index].track_stereo_enc
    track_multi_enc = channel_infos[channel_index].track_multi_enc

    osc_reaper.send_message(
        f"/track/{track_stereo_enc}/fx/1/fxparam/1/value", stereo_gain)
    osc_reaper.send_message(
        f"/track/{track_stereo_enc}/fx/1/fxparam/15/value", stereo_gain)
    osc_reaper.send_message(
        f"/track/{track_multi_enc}/fx/1/fxparam/1/value", multi_gain)


def slope_constant_power(value):
    resolution = np.arange(start=0, stop=1, step=0.1)
    slope = [0, 0.4, 0.6, 0.70, 0.75, 0.77, 0.80, 0.85, 0.9, 1]
    val = np.interp(value, resolution, slope)
    return val

def slope_3d(value):
    resolution = np.arange(start=0, stop=1, step=0.1)
    slope = [0, 0.3, 0.4, 0.5, 0.6, 0.7, 0.75, 0.8, 0.9, 1]
    val = np.interp(value, resolution, slope)
    return val

def slope_volume(value):
    val = np.interp(value, [0, 1], [0, 0.5])
    return val

def slope_eq(value):
    resolution = np.arange(start=0, stop=1, step=0.1)
    slope = [0.0, 0.1, 0.2, 0.3, 0.5, 0.52, 0.54, 0.56, 0.58, 0.6]
    val = np.interp(value, resolution, slope)
    #val = np.interp(value, [0, 1], [0, 0.6])
    return val

def slope_fx_gain(value):
    val = np.interp(value, [0, 1], [0, 0.6])
    return val

def slope_fx_freq_hipass(value):
    val = np.interp(value, [0, 1], [0.2, 0.8])
    return val

def slope_fx_freq_lopass(value):
    val = np.interp(value, [0, 1], [0.8, 0.2])
    return val

def slope_fx_res(value):
    val = np.interp(value, [0, 1], [0, 1])
    return val

def slope_crossover_1b(value):
    db = 20 * np.log10(np.clip(value, 1e-10, 1))
    val = (db + 120) / 120 * 0.5 
    return np.clip(val, 0, 0.5)

def slope_crossover_1a(value):
    db = 20 * np.log10(np.clip(value, 1e-10, 1))
    val_tmp = (db + 120) / 120 * 0.5
    val = 0.5 - val_tmp
    return np.clip(val, 0, 0.5)

def slope_crossfade_gain(control_value):
    overlap = 4.5
    min_db = -40
    max_db = 0
    
    angle = control_value * np.pi / 2
    db1 = (np.cos(angle) ** (2 / overlap)) * max_db + (1 - np.cos(angle) ** (2 / overlap)) * min_db
    db2 = (np.sin(angle) ** (2 / overlap)) * max_db + (1 - np.sin(angle) ** (2 / overlap)) * min_db
    gain1 = (db1 - min_db) / (max_db - min_db) * 0.5
    gain2 = (db2 - min_db) / (max_db - min_db) * 0.5
    
    return gain1, gain2

def set_filters() -> None:
    for channel_index in range(4):
        for fx_index, bypass_active in (
                (FX_INDEX_LOPASS,
                 (not channel_infos[channel_index].toggle_fx or
                  master_info.fx_mode == MasterInfo.FXMode.HIGH_PASS)),
                (FX_INDEX_HIPASS,
                 (not channel_infos[channel_index].toggle_fx or
                  master_info.fx_mode == MasterInfo.FXMode.LOW_PASS))):

            message = ("/track/"
                       f"{channel_infos[channel_index].track_input}"
                       f"/fx/{fx_index}/bypass")

            # osc_reaper expects 1 for "plugin active" and 0 for bypass
            osc_reaper.send_message(message, float(not bypass_active))

def send_elevation(channel_index):
    elevation = channel_infos[channel_index].elevation
    normalized_value = np.interp(elevation, [-180, 180], [0, 1])
    track_stereo_enc = channel_infos[channel_index].track_stereo_enc
    osc_reaper.send_message(
        f"/track/{track_stereo_enc}/fx/{FX_INDEX_STEREO_ENC}/fxparam/8/value", normalized_value)

def param_handler(address: str,
                  *osc_arguments: List[Any]) -> None:

    words: List[str] = address.split("/")
    section: str = words[3]
    parameter: str = words[4]

    #  mypy 0.920 reports a false positive, retest!
    value: float = float(osc_arguments[0])  # type: ignore
    assert type(value) == float
    print(section + "." + parameter + " : " + str(value))

    for channel_index in range(4):
        if section == str(channel_index):
            param_handler_channel(channel_index, parameter, value)

    if section == "master":
        param_handler_master(parameter, value)

    elif section.startswith("fx"):
        param_handler_fx(section, parameter, value)

def osc_handler_channel(client_address: Tuple[str, int], address: str,
                        *osc_arguments: List[Any]) -> None:

    # The buttons below read the argument as it arrived rather than as a
    # float, because its type is what tells the mixer's momentary edge from
    # A3 Motion's state. Everything else here is continuous and wants the
    # number. See a3_core_buttons.
    raw: Any = osc_arguments[0]

    #  mypy 0.920 reports a false positive, retest!
    value: float = float(raw)  # type: ignore
    assert type(value) == float

    traffic.seen(IN, address, raw,
                 peer_name(client_address[0], PEER_HOSTS,
                           only=COMMANDERS))
    if _print_osc:
        print(address + " : " + str(value))

    words: List[str] = address.split("/")
    channel: str = words[2]
    parameter: str = words[3]

    channel_index = int(channel)
    track_input = channel_infos[channel_index].track_input

    # POTENTIOMETER

    # fx-send drives the stereo/multi crossfade, which is not what its name
    # says, and that is on purpose for now.
    #
    # The 3D function used to be reached through the mixer's FX-send knob --
    # that was the only continuous control there was for it. A3 Motion's
    # per-channel pot does it now, on /channel/n/3d, and that is new. Until
    # the mixer stops sending fx-send for this, both roads have to arrive:
    # taking this one away would take the 3D function off the mixer before
    # anyone had agreed to that.
    #
    # So the channel FX send itself does nothing at the moment. See
    # issues/a3-core-fx-send-fuehrt-noch-die-3d-funktion.md for what has to be
    # true before this block goes and the send below comes back.
    if parameter == "fx-send":
        # The A3 Mixer's pot, which carries the 3D crossfade and not the fx
        # send its name promises. Same function as `3d` below, and it writes
        # the same memory -- turning this pot and then asking for a recall
        # must not answer with what the other road last said.
        send_crossfade(channel_index, value)

    # What 3d is for: A3 Motion's per-channel pot, crossfading the channel
    # between its stereo and its multi encoder. The same curves as fx-send
    # above, which is the road this arrived by until now.
    if parameter == "3d":
        send_crossfade(channel_index, value)

    elif parameter == "gain":
        val = slope_volume(value)
        osc_reaper.send_message(f"/track/{track_input}/fx/{FX_INDEX_GAIN}/fxparam/1/value", val)

    elif parameter == "eq":
        eq_parameter : str = words[4]
        if eq_parameter == "high":
            val = slope_eq(value)
            osc_reaper.send_message(
                f"/track/{track_input}/fx/{FX_INDEX_EQ}/fxparam/1/value", val) # Smooth-EQ (airwindows)

        elif eq_parameter == "mid":
            val = slope_eq(value)
            osc_reaper.send_message(
                f"/track/{track_input}/fx/{FX_INDEX_EQ}/fxparam/2/value", val) # Smooth-EQ (airwindows)

        elif eq_parameter == "low":
            val = slope_eq(value)
            osc_reaper.send_message(
                f"/track/{track_input}/fx/{FX_INDEX_EQ}/fxparam/3/value", val) # Smooth-EQ (airwindows)
    
    elif parameter == "volume":
        val = slope_volume(value)
        track_channelbus = channel_infos[channel_index].track_channelbus
        for gain_vst_plugins_on_channelbus in _layout.gain_params("channelbus"):
            osc_reaper.send_message(
                f"/track/{track_channelbus}/fx/1/fxparam/{gain_vst_plugins_on_channelbus}/value", val)

    # BUTTONS

    elif parameter == "pfl":
        wanted = wanted_toggle(raw, channel_infos[channel_index].toggle_pfl)
        if wanted is not NO_CHANGE:
            channel_infos[channel_index].toggle_pfl = wanted
            track_pfl = channel_infos[channel_index].track_pfl
            muted = not channel_infos[channel_index].toggle_pfl
            osc_reaper.send_message(
                f"/track/{track_pfl}/mute", float(muted))
            osc_a3mixer.send_message(
                *led_message(_layout, "pfl", channel_index,
                             channel_infos[channel_index]))

    elif parameter == "fx":
        wanted = wanted_toggle(raw, channel_infos[channel_index].toggle_fx)
        if wanted is not NO_CHANGE:
            channel_infos[channel_index].toggle_fx = wanted
            osc_a3mixer.send_message(
                *led_message(_layout, "fx", channel_index,
                             channel_infos[channel_index]))
            set_filters()

    elif parameter == "4d":
        wanted = wanted_toggle(raw, channel_infos[channel_index].toggle_3d)
        if wanted is not NO_CHANGE:
            channel_infos[channel_index].toggle_3d = wanted
            is_enabled = channel_infos[channel_index].toggle_3d
            osc_a3mixer.send_message(
                *led_message(_layout, "3d", channel_index,
                             channel_infos[channel_index]))
            track_stereo_enc = channel_infos[channel_index].track_stereo_enc
            track_multi_enc = channel_infos[channel_index].track_multi_enc
            osc_val = 0.5 if is_enabled else 0.0
            osc_val_inverse = 0.5 if not is_enabled else 0.0
            for gain_vst_plugins_on_channelbus in _layout.gain_params("channelbus"):
                osc_reaper.send_message(
                    f"/track/{track_stereo_enc}/fx/1/fxparam/{gain_vst_plugins_on_channelbus}/value",
                    osc_val
                )
            osc_reaper.send_message(
                f"/track/{track_multi_enc}/fx/1/fxparam/1/value",
                osc_val_inverse
            )

    # A3MOTION

    if parameter == "azimuth":
        # clamp -180..180 und sende als float an alle IEM-Empfänger
        az = float(max(min(value, 180.0), -180.0))
        # The clamped value, not the one that arrived: a recall has to replay
        # what the plugins were actually told, or the first recall after an
        # out-of-range message would move the sound.
        channel_infos[channel_index].azimuth = az
        addr = f"/MultiEncoder/azimuth{channel_index}"
        for client in udp_clients_iem:
            client.send_message(addr, az)

    elif parameter == "elevation":
        # clamp -90..90 und sende als float an alle IEM-Empfänger
        el = float(max(min(value, 90.0), -90.0))
        channel_infos[channel_index].elevation = el
        addr = f"/MultiEncoder/elevation{channel_index}"
        for client in udp_clients_iem:
            client.send_message(addr, el)

    elif parameter == "pot_1":
        val = np.interp(value, [0, 1], [0.05, 0.9])
        track_stereo_enc = channel_infos[channel_index].track_stereo_enc
        #osc_reaper.send_message(
        #    f"/track/{track_stereo_enc}/fx/2/fxparam/1/value", value)
        osc_reaper.send_message(
            f"/track/{track_stereo_enc}/fx/{FX_INDEX_ENC_POTS}"
            f"/fxparam/{_layout.fx_param('enc_pot_1')}/value", val)
        track_stereo_enc = channel_infos[channel_index].track_stereo_enc

    elif parameter == "pot_2":
        val = np.interp(value, [0, 1], [0.05, 0.9])
        track_stereo_enc = channel_infos[channel_index].track_stereo_enc
        #osc_reaper.send_message(
        #    f"/track/{track_stereo_enc}/fx/2/fxparam/2/value", value)
        osc_reaper.send_message(
            f"/track/{track_stereo_enc}/fx/{FX_INDEX_ENC_POTS}"
            f"/fxparam/{_layout.fx_param('enc_pot_2')}/value", val)
        track_stereo_enc = channel_infos[channel_index].track_stereo_enc

    remember_state()

def osc_handler_master(client_address: Tuple[str, int], address: str,
                       *osc_arguments: List[Any]) -> None:

    #  mypy 0.920 reports a false positive, retest!
    value: float = float(osc_arguments[0])  # type: ignore
    assert type(value) == float

    traffic.seen(IN, address, osc_arguments[0],
                 peer_name(client_address[0], PEER_HOSTS,
                           only=COMMANDERS))
    if _print_osc:
        print(address + " : " + str(value))

    words: List[str] = address.split("/")
    parameter: str = words[2]

    if parameter == "volume":
        val = slope_volume(value)
        masterbus = master_info.track_masterbus
        for gain_vst_plugins_on_masterbus in _layout.gain_params("masterbus"):
            osc_reaper.send_message(f"/track/{masterbus}/fx/1/fxparam/{gain_vst_plugins_on_masterbus}/value", val)

    if parameter == "booth":
        val = slope_volume(value)
        boothbus = master_info.track_booth
        for gain_vst_plugins_on_boothbus in _layout.gain_params("boothbus"):
            osc_reaper.send_message(f"/track/{boothbus}/fx/1/fxparam/{gain_vst_plugins_on_boothbus}/value", val)

    if parameter == "phones_mix":
        track_ph_mix = master_info.track_ph_mix
        val = value * 0.5    
        #val = slope_crossover_1a(value)
        inv_val = 1 - value
        osc_reaper.send_message(f"/track/{track_ph_mix}/volume", value)
        for channel_index in range(4):
            track_pfl = channel_infos[channel_index].track_pfl
            osc_reaper.send_message(f"/track/{track_pfl}/volume", inv_val)
        

        #for mix_param in [1, 15, 29, 43, 57, 71, 85, 99]:
        #    osc_reaper.send_message(f"/track/{track_ph_mix}/fx/1/fxparam/{mix_param}/value", val)
        #for channel_index in range(4):
        #    track_pfl = channel_infos[channel_index].track_pfl
        #    for pfl_param in [1, 15]:
        #        osc_reaper.send_message(f"/track/{track_pfl}/fx/1/fxparam/{pfl_param}/value", inv_val)

    if parameter == "phones_volume":
        val = slope_volume(value)
        track_phones = master_info.track_phones
        osc_reaper.send_message(f"/track/{track_phones}/fx/2/fxparam/1/value", val)

    elif parameter == "return":
        val = slope_constant_power(value)
        aux_return = master_info.aux_return
        for gain_vst_plugins_on_return in _layout.gain_params("aux_return"):
            osc_reaper.send_message(f"/track/{aux_return}/fx/3/fxparam/{gain_vst_plugins_on_return}/value", val)

def osc_handler_fx(client_address: Tuple[str, int], address: str,
                   *osc_arguments: List[Any]) -> None:

    value = osc_arguments[0]

    traffic.seen(IN, address, value,
                 peer_name(client_address[0], PEER_HOSTS,
                           only=COMMANDERS))
    if _print_osc:
        print(address + " : " + str(value))

    words: List[str] = address.split("/")
    parameter: str = words[2]

    if parameter == "mode":
        # The mixer names the mode ("high_pass"), A3 Motion sends the number
        # its own key reads (1 is HPF). What used to stand here compared
        # against the word alone, so every number -- every message Motion has
        # ever sent on this address -- meant LOW_PASS. See a3_core_buttons.
        wanted = wanted_fx_mode(value, master_info.fx_mode.name.lower())
        master_info.fx_mode = MasterInfo.FXMode[wanted.upper()]
        osc_a3mixer.send_message(
            _layout.address("fx_mode_led"),
            FX_MODE_WORDS[master_info.fx_mode.name])
        set_filters()

    elif parameter == "frequency":
        val_hipass = slope_fx_freq_hipass(value)
        val_lopass = slope_fx_freq_lopass(value)
        for channel_index in range(4):
            track_input = channel_infos[channel_index].track_input
            osc_reaper.send_message(f"/track/{track_input}/fx/{FX_INDEX_HIPASS}/fxparam/7/value", val_hipass)
            osc_reaper.send_message(f"/track/{track_input}/fx/{FX_INDEX_LOPASS}/fxparam/7/value", val_lopass)

    elif parameter == "resonance":
        val = slope_fx_res(value)
        for channel_index in range(4):
            track_input = channel_infos[channel_index].track_input
            osc_reaper.send_message(f"/track/{track_input}/fx/{FX_INDEX_HIPASS}/fxparam/6/value", val)
            osc_reaper.send_message(f"/track/{track_input}/fx/{FX_INDEX_LOPASS}/fxparam/6/value", val)

    remember_state()

def osc_handler_tap(address: str,
                   *osc_arguments: List[Any]) -> None:

    value = osc_arguments[0]

    # print(address + " : " + str(value))

    words: List[str] = address.split("/")
    parameter: str = words[1]

    if parameter == "tap" and value == "1":
        note = [0x90, 60, 0] # Clock tap
        midiout.send_message(note)

#: Where a device asks Core to say the state again. One address rather than
#: one per value: the answer is the ordinary messages, so nothing new has to
#: be understood at the other end.
OSC_ADDRESS_RECALL: str = "/state/recall"


def osc_handler_recall(client_address: Tuple[str, int], address: str,
                       *osc_arguments: List[Any]) -> None:
    """Say the whole state again, as the messages it would have arrived as.

    Sent to the device each value belongs to rather than back to whoever
    asked. A mixer being told the lights it already shows is a repaint; a
    mixer *not* being told because Motion happened to be the one that asked
    would be a rig where two devices disagree and neither can find out.
    """
    # The one address whose whole purpose is "did the other end come back?"
    # -- if this is not tapped, a device returning after a drop is invisible
    # in the window, which is exactly the case the window exists to show.
    traffic.seen(IN, address, osc_arguments[0] if osc_arguments else None,
                 peer_name(client_address[0], PEER_HOSTS,
                           only=COMMANDERS))

    messages = list(recall_messages(_layout, channel_infos, master_info,
                                    _relayed))
    for device, out, value in messages:
        client_for(device).send_message(out, value)
    print(f"{address}: replayed {len(messages)} messages")


#: Where REAPER's feedback is heard. Its own port, not Core's: REAPER speaks
#: /track/* and /fx/*, and /fx/* is what the mixer uses for its filter -- one
#: port for both would have Core reading REAPER's reports as commands and
#: answering them, which is a loop on a rig that makes sound.
OSC_PORT_REAPER_FEEDBACK: int = 9002

_curves = load_curves(json.loads(CURVES_PATH.read_text()))


def reaper_feedback_handler(client_address: Tuple[str, int], address: str,
                            *osc_arguments: List[Any]) -> None:
    """One value REAPER reports, on its way back to the device that set it.

    Everything Core does not recognise is counted and left alone. That is most
    of what arrives -- REAPER reports names, strings, sends, pans and the
    decibel spelling of every volume -- and none of it is A3's.
    """
    if not osc_arguments:
        return

    try:
        value = float(osc_arguments[0])   # type: ignore
    except (TypeError, ValueError):
        return                            # a name or a string, not a value

    # Core's own confirmation. Passing it on would tell the mixer what the
    # mixer just said, after a trip through a curve and back -- and on the
    # curves with plateaus it would come back changed.
    if echo_filter.is_echo(address, value):
        return

    # The tap used to sit here, before Core knew whether it could route the
    # message -- so every one of REAPER's ~19,000 reported addresses became a
    # permanent row, the snapshot grew to 6.4 MiB, and the stream pushed it at
    # 25.6 MiB/s until the maintainer's machine froze on 2026-09-10. It now
    # happens twice, further down: traffic.seen() on the path that succeeds,
    # traffic.unknown() on each path that gives up.
    peer = peer_name(client_address[0], PEER_HOSTS, only=ANSWERERS)

    parts = address.strip("/").split("/")
    if len(parts) < 2 or parts[0] != "track":
        traffic.unknown(address, value, peer)
        return

    try:
        track = int(parts[1])
    except ValueError:
        traffic.unknown(address, value, peer)
        return

    role = _layout.track_role(track)
    if role is None:
        traffic.unknown(address, value, peer)   # master, or a track A3 does not name
        return

    channel_index, field = role
    entry = reverse_for(_layout, address, field)
    if entry is None:
        traffic.unknown(address, value, peer)
        return

    try:
        a3_value = invert(_curves[entry.curve], value)
    except (CurveNotInvertible, KeyError):
        traffic.unknown(address, value, peer)
        return

    out = _layout.address("channel_control", channel=channel_index,
                          control=entry.address)
    traffic.seen(IN, address, value, peer)
    _relayed.note(entry.to, out, a3_value)
    client_for(entry.to).send_message(out, a3_value)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="0.0.0.0", help="The ip to listen on")
    parser.add_argument("--port", type=int,
                        default=OSC_PORT_CORE, help="The port to listen on")
    parser.add_argument("--feedback-port", type=int,
                        default=OSC_PORT_REAPER_FEEDBACK,
                        help="The port REAPER reports back on")
    parser.add_argument("--mixer", default=f"{A3MIXER_HOST}:{A3MIXER_PORT}",
                        help="host:port of A3 Mixer")
    parser.add_argument("--motion", default=f"{A3MOTION_HOST}:{A3MOTION_PORT}",
                        help="host:port of A3 Motion")
    parser.add_argument("--reaper", default=f"{REAPER_HOST}:{REAPER_PORT}",
                        help="host:port of REAPER's OSC input. Point it "
                             "somewhere else to exercise this without "
                             "driving the rig.")
    parser.add_argument("--print-osc", action="store_true",
                        help="Also print every message, the way Core did "
                             "before the window existed. Off by default: it "
                             "was 301,385 journal lines an hour on one "
                             "address alone, which is what made the journal "
                             "unsearchable.")
    parser.add_argument("--web-bind", default="127.0.0.1:9080",
                        help="host:port for the window. Localhost by "
                             "default: the window can send OSC into a "
                             "running rig, and a control surface with no "
                             "login on the show network is not a default "
                             "worth setting.")
    parser.add_argument("--no-web", action="store_true",
                        help="Do not open the window at all.")
    args = parser.parse_args()

    _print_osc = args.print_osc

    PEER_HOSTS = {"mixer": args.mixer.rpartition(":")[0],
                  "motion": args.motion.rpartition(":")[0],
                  "reaper": args.reaper.rpartition(":")[0]}

    # Pointed somewhere else for a bench run. Rebound at module scope, which
    # is where the handlers read them -- there is no main() here, the argument
    # parsing runs under `if __name__` at the top level. (A `global`
    # declaration was tried first and is a syntax error there, since the names
    # are already bound above.)
    for name, spec in (("mixer", args.mixer), ("motion", args.motion),
                       ("reaper", args.reaper)):
        host, _, port = spec.rpartition(":")
        client = WatchedClient(SimpleUDPClient(host, int(port)), name)
        if name == "mixer":
            osc_a3mixer = client
        elif name == "motion":
            osc_a3motion = client
        else:
            osc_reaper = client

    dispatcher = dispatcher.Dispatcher()

    # needs_reply_address makes the dispatcher call
    # handler(client_address, address, *args) instead of
    # handler(address, *args). It is set on every map here, not only where
    # the address is used: a handler whose signature depends on which of two
    # ways it was mapped is a trap, and the window wants all of them.
    #
    # The address is (host, ephemeral_port). Only the host is any use for
    # naming a sender: the client never binds, so the OS assigns its source
    # port on the socket's first send and keeps it for that socket's life --
    # stable, but unpredictable and carrying no identity of its own. Two
    # senders on one host get two different ports that say "not the same
    # sender" and never say which one. See a3_core_traffic.peer_name.
    #
    # What *is* of use is which port the message arrived at, which is fixed
    # and known here: everything mapped on this dispatcher came in on Core's
    # main port, so only a controller can have sent it. Hence only=COMMANDERS
    # at every tap below, and only=ANSWERERS on the feedback dispatcher's.
    dispatcher.map("/channel/*", osc_handler_channel,
                   needs_reply_address=True)
    dispatcher.map("/master/*", osc_handler_master, needs_reply_address=True)
    dispatcher.map("/fx/*", osc_handler_fx, needs_reply_address=True)
    dispatcher.map(OSC_ADDRESS_RECALL, osc_handler_recall,
                   needs_reply_address=True)
    #dispatcher.map("/tap", osc_handler_tap)

    # Motion-Controller
    # dispatcher.map("/CoordinateConverter/*", iemToCtrlMotion_handler)
    # dispatcher.map("/moc/channel/*", ctrlMotionToIem_handler)
    # dispatcher.map("/moc/channel/*", ctrlMotionToIem_handler)

    # REAPER's feedback, on its own port and its own dispatcher.
    #
    # Separate because REAPER speaks /track/* and /fx/*, and /fx/* is the
    # mixer's filter -- one port for both would have Core reading REAPER's
    # reports as commands and answering them, which is a loop on a rig that
    # makes sound.
    #
    # In a thread of its own so a burst does not hold up the commands coming
    # in on the main port: REAPER sends twenty-five thousand messages when the
    # surface reconnects, and a set does not wait for that.
    feedback_dispatcher = osc_dispatcher.Dispatcher()
    feedback_dispatcher.set_default_handler(reaper_feedback_handler,
                                            needs_reply_address=True)
    feedback_server = osc_server.ThreadingOSCUDPServer(
        (args.ip, args.feedback_port), feedback_dispatcher)
    threading.Thread(target=feedback_server.serve_forever,
                     daemon=True).start()
    print(f"listening for REAPER feedback on "
          f"{args.ip}:{args.feedback_port}")

    # A stop is a stop, but the last change may still be inside the state
    # file's delay. systemd stops this with SIGTERM and a hand with SIGINT,
    # and neither runs anything by itself -- without this, switching the
    # filter and immediately stopping Core forgets the switch.
    def stop(signum, frame):
        _state_file.flush()
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    # What the bench sends with. A function rather than the clients
    # themselves, because --mixer and --motion rebind those at module scope
    # and a captured reference would go on talking to the old address.
    #
    # "self" is Core's own port, which is how /state/recall is reached: it is
    # a message Core handles, not one it forwards.
    #
    # This branch bypasses WatchedClient on purpose, so it gets no
    # echo_filter.sent() and no traffic.seen(OUT, ...): the message loops
    # straight back into Core's own OSC port and is recorded there as IN --
    # wrapping it here too would count the one message twice.
    def send_from_bench(to, address, value):
        if to == "self":
            SimpleUDPClient("127.0.0.1", args.port).send_message(
                address, value)
            return
        {"mixer": osc_a3mixer, "motion": osc_a3motion,
         "reaper": osc_reaper}[to].send_message(address, value)

    # The window, if it will come. Its failure is not Core's: a busy port
    # gets a line in the journal and the rig still makes sound.
    if not args.no_web:
        if start_window(traffic, args.web_bind, send=send_from_bench):
            # window_address(), not args.web_bind: --web-bind accepts port 0
            # to let the OS choose one, and printing the requested bind would
            # then log "http://127.0.0.1:0" while the real port stays
            # unknowable from the journal alone.
            host, port = window_address()
            print(f"window on http://{host}:{port}")

    server = osc_server.ThreadingOSCUDPServer((args.ip, args.port), dispatcher)
    print("Serving on {}".format(server.server_address))
    server.serve_forever()
