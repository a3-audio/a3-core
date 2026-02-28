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
import numpy as np
import time
#import rtmidi
import math
from typing import List, Any
from enum import Enum
from dataclasses import dataclass
from pythonosc import dispatcher  # type: ignore
from pythonosc import osc_server
from pythonosc.udp_client import SimpleUDPClient  # type: ignore

OSC_PORT_CORE: int = 9000

FX_INDEX_GAIN: int = 1
FX_INDEX_EQ: int = 2
FX_INDEX_EQ_ENC: int = 3
FX_INDEX_HIPASS: int = 3
FX_INDEX_LOPASS: int = 4
FX_INDEX_CHANNEL_VOLUME: int = 1
FX_INDEX_STEREO_ENC: int = 4
FX_INDEX_ENC: int = 1

CHANNEL_ENC_MAIN: int = 26
CHANNEL_ENC_PHONES: int = 27
CHANNEL_ENC_DELAY: int = 25

# OSC clients
osc_a3mixer = SimpleUDPClient('192.168.43.55', 7771)
osc_a3motion = SimpleUDPClient('192.168.43.54', 8700)
osc_reaper = SimpleUDPClient('127.0.0.1', 9001) #57
# osc_vid = SimpleUDPClient('192.168.43.100', 7771)

udp_clients_iem = tuple(SimpleUDPClient('127.0.0.1', 1337 + index)
                        for index in range(3))

@dataclass
class MasterInfo:
    track_masterbus: int = 1
    track_booth: int = 2
    
    track_phones: int = 3
    track_ph_mix: int = 8
    
    aux_return: int = 25
    
    class FXMode(Enum):
        LOW_PASS = 0
        HIGH_PASS = 1
    fx_mode: FXMode = FXMode.LOW_PASS

master_info = MasterInfo()

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

    # we cache elevation and width because elevation is used to
    # recalculate the width, which is narrowed towards the zenith.
    elevation: float = 0.0
    width: float = 0.0

channel_infos = (
    # Channel 1
    ChannelInfo(
        enc_main_azimuth=8,
        enc_main_elevation=9,
        enc_phones_solo=12,
        track_input=12,
        track_multi_enc=11,
        track_stereo_enc=10,
        track_channelbus=9,
        track_pfl=4,
    ),
    # Channel 2
    ChannelInfo(
        enc_main_azimuth=13,
        enc_main_elevation=14,
        enc_phones_solo=17,
        track_input=16,
        track_multi_enc=15,
        track_stereo_enc=14,
        track_channelbus=13,
        track_pfl=5,
    ),
    # Channel 3
    ChannelInfo(
        enc_main_azimuth=18,
        enc_main_elevation=19,
        enc_phones_solo=22,
        track_input=20,
        track_multi_enc=19,
        track_stereo_enc=18, 
        track_channelbus=17,
        track_pfl=6,
    ),
    # Channel 4
    ChannelInfo(
        enc_main_azimuth=23,
        enc_main_elevation=24,
        enc_phones_solo=27,
        track_input=24,
        track_multi_enc=23,
        track_stereo_enc=22,
        track_channelbus=21,
        track_pfl=7,
    ),
)

def slope_constant_power(value):
    resolution = np.arange(start=0, stop=1, step=0.1)
    slope = [0, 0.4, 0.6, 0.70, 0.75, 0.77, 0.80, 0.85, 0.9, 1]
    val = np.interp(value, resolution, slope)
    return val

def slope_volume(value):
    val = np.interp(value, [0, 1], [0, 0.5])
    return val

def slope_eq(value):
    resolution = np.arange(start=0, stop=1, step=0.1)
    slope = [0.0, 0.1, 0.2, 0.3, 0.5, 0.52, 0.54, 0.56, 0.58, 0.6]
    val = np.interp(value, resolution, slope)
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

def osc_handler_channel(address: str,
                        *osc_arguments: List[Any]) -> None:

    #  mypy 0.920 reports a false positive, retest!
    value: float = float(osc_arguments[0])  # type: ignore
    assert type(value) == float

    print(address + " : " + str(value))

    words: List[str] = address.split("/")
    channel: str = words[2]
    parameter: str = words[3]

    channel_index = int(channel)
    track_input = channel_infos[channel_index].track_input

    # POTENTIOMETER

    if parameter == "fx-send":
        val = slope_constant_power(value)
        track_channelbus = channel_infos[channel_index].track_channelbus
        osc_reaper.send_message(
            f"/track/{track_channelbus}/send/3/volume", val)

    elif parameter == "gain":
        #val = (value)
        #osc_reaper.send_message(f"/track/{track_input}/fx/{FX_INDEX_GAIN}/fxparam/1/value", val)
        #osc_reaper.send_message(f"/track/{track_input}/volume", value)
        #phones_mix = slope_crossover_1a(value)
        #phones_pfl = slope_crossover_1b(value)
        #track_stereo_enc = channel_infos[channel_index].track_stereo_enc
        #track_multi_enc = channel_infos[channel_index].track_multi_enc
        #osc_reaper.send_message(f"/track/{track_multi_enc}/fx/{FX_INDEX_GAIN}/fxparam/1/value", phones_mix)
        #osc_reaper.send_message(f"/track/{track_stereo_enc}/fx/{FX_INDEX_GAIN}/fxparam/1/value", phones_pfl)
        val = slope_volume(value)
        track_stereo_enc = channel_infos[channel_index].track_stereo_enc
        osc_reaper.send_message(
            f"/track/{track_stereo_enc}/fx/2/fxparam/4/value", val)

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
        for gain_vst_plugins_on_channelbus in [1, 15]:
            osc_reaper.send_message(
                f"/track/{track_channelbus}/fx/1/fxparam/{gain_vst_plugins_on_channelbus}/value", val)

    # BUTTONS

    elif parameter == "pfl" and value == 1:
        channel_infos[channel_index].toggle_pfl = (
            not channel_infos[channel_index].toggle_pfl)
        track_pfl = channel_infos[channel_index].track_pfl
        muted = not channel_infos[channel_index].toggle_pfl
        osc_reaper.send_message(
            f"/track/{track_pfl}/mute", float(muted))
        osc_a3mixer.send_message(
            f"/channel/{channel_index}/led/pfl", float(muted))

    elif parameter == "fx" and value == 1:
        channel_infos[channel_index].toggle_fx = (
            not channel_infos[channel_index].toggle_fx)
        is_enabled = channel_infos[channel_index].toggle_fx
        osc_a3mixer.send_message(
            f"/channel/{channel_index}/led/fx", float(is_enabled))
        set_filters()

    elif parameter == "3d" and value == 1:
        channel_infos[channel_index].toggle_3d = (
            not channel_infos[channel_index].toggle_3d
        )
        is_enabled = channel_infos[channel_index].toggle_3d
        osc_a3mixer.send_message(
            f"/channel/{channel_index}/led/3d",
            float(is_enabled)
        )
        track_stereo_enc = channel_infos[channel_index].track_stereo_enc
        track_multi_enc = channel_infos[channel_index].track_multi_enc
        osc_val = 0.5 if is_enabled else 0.0
        osc_val_inverse = 0.5 if not is_enabled else 0.0
        for gain_vst_plugins_on_channelbus in [1, 15]:
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
        addr = f"/MultiEncoder/azimuth{channel_index}"
        for client in udp_clients_iem:
            client.send_message(addr, az)

    elif parameter == "elevation":
        # clamp -90..90 und sende als float an alle IEM-Empfänger
        el = float(max(min(value, 90.0), -90.0))
        addr = f"/MultiEncoder/elevation{channel_index}"
        for client in udp_clients_iem:
            client.send_message(addr, el)

    elif parameter == "pot_1":
        val = np.interp(value, [0, 1], [0.05, 0.9])
        track_stereo_enc = channel_infos[channel_index].track_stereo_enc
        osc_reaper.send_message(
            f"/track/{track_stereo_enc}/fx/2/fxparam/1/value", value)

    elif parameter == "pot_2":
        val = np.interp(value, [0, 1], [0.05, 0.9])
        track_stereo_enc = channel_infos[channel_index].track_stereo_enc
        osc_reaper.send_message(
            f"/track/{track_stereo_enc}/fx/2/fxparam/2/value", value)

def osc_handler_master(address: str,
                       *osc_arguments: List[Any]) -> None:

    #  mypy 0.920 reports a false positive, retest!
    value: float = float(osc_arguments[0])  # type: ignore
    assert type(value) == float

    print(address + " : " + str(value))

    words: List[str] = address.split("/")
    parameter: str = words[2]

    if parameter == "volume":
        val = slope_volume(value)
        masterbus = master_info.track_masterbus
        for gain_vst_plugins_on_masterbus in [1,15,29,43,57,71,85,99]:
            osc_reaper.send_message(f"/track/{masterbus}/fx/1/fxparam/{gain_vst_plugins_on_masterbus}/value", val)

    if parameter == "booth":
        val = slope_volume(value)
        boothbus = master_info.track_booth
        for gain_vst_plugins_on_boothbus in [1,15,29,43,57,71,85,99]:
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
        for gain_vst_plugins_on_return in [1,15,29,43,57,71,85,99]:
            osc_reaper.send_message(f"/track/{aux_return}/fx/3/fxparam/{gain_vst_plugins_on_return}/value", val)

def osc_handler_fx(address: str,
                   *osc_arguments: List[Any]) -> None:

    value = osc_arguments[0]

    print(address + " : " + str(value))

    words: List[str] = address.split("/")
    parameter: str = words[2]

    if parameter == "mode":
        high_pass = value == "high_pass"
        master_info.fx_mode = MasterInfo.FXMode.HIGH_PASS if high_pass else MasterInfo.FXMode.LOW_PASS
        osc_a3mixer.send_message("/fx/led", "high_pass" if high_pass else "low_pass")
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

def osc_handler_tap(address: str,
                   *osc_arguments: List[Any]) -> None:

    value = osc_arguments[0]

    # print(address + " : " + str(value))

    words: List[str] = address.split("/")
    parameter: str = words[1]

    if parameter == "tap" and value == "1":
        note = [0x90, 60, 0] # Clock tap
        midiout.send_message(note)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="0.0.0.0", help="The ip to listen on")
    parser.add_argument("--port", type=int,
                        default=OSC_PORT_CORE, help="The port to listen on")
    args = parser.parse_args()

    dispatcher = dispatcher.Dispatcher()

    dispatcher.map("/channel/*", osc_handler_channel)
    dispatcher.map("/master/*", osc_handler_master)
    dispatcher.map("/fx/*", osc_handler_fx)
    #dispatcher.map("/tap", osc_handler_tap)

    # Motion-Controller
    # dispatcher.map("/CoordinateConverter/*", iemToCtrlMotion_handler)
    # dispatcher.map("/moc/channel/*", ctrlMotionToIem_handler)
    # dispatcher.map("/moc/channel/*", ctrlMotionToIem_handler)

    server = osc_server.ThreadingOSCUDPServer((args.ip, args.port), dispatcher)
    print("Serving on {}".format(server.server_address))
    server.serve_forever()
