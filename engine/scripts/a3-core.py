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
from typing import List, Any
from enum import Enum
from dataclasses import dataclass
from pythonosc import dispatcher  # type: ignore
from pythonosc import osc_server
from pythonosc.udp_client import SimpleUDPClient  # type: ignore

OSC_PORT_CORE: int = 9000

FX_INDEX_HIPASS: int = 2
FX_INDEX_LOPASS: int = 3

# OSC clients
osc_mic = SimpleUDPClient('192.168.43.51', 7771)
osc_moc = SimpleUDPClient('192.168.43.52', 8700)
osc_reaper = SimpleUDPClient('127.0.0.1', 9001)
osc_vid = SimpleUDPClient('192.168.43.102', 7771)

udp_clients_iem = tuple(SimpleUDPClient('127.0.0.1', 1337 + index)
                        for index in range(4))

# Midi client
#midiout = rtmidi.MidiOut()
#available_ports = midiout.get_ports()

#if available_ports:
#    midiout.open_port(0)
#else:
#    midiout.open_virtual_port("a3 osc router")

@dataclass
class MasterInfo:
    track_masterbus: int = 1
    track_master_booth: int = 5
    track_booth: int = 6
    
    track_master_phones: int = 17
    track_phones: int = 10
    track_mainmixbus: int = 18
    
    track_master_rec: int = 35
    
    track_reverb_binaural: int = 38
    track_reverb_stereo: int = 39

    class FXMode(Enum):
        LOW_PASS = 0
        HIGH_PASS = 1
    fx_mode: FXMode = FXMode.LOW_PASS

master_info = MasterInfo()

@dataclass
class ChannelInfo:
    track_input: int
    track_stereo: int
    track_channelbus: int
    track_pfl: int
    track_bformat: int

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
        track_input=22,
        track_stereo=21,
        track_bformat=20,
        track_channelbus=19,
        track_pfl=14,
    ),
    # Channel 2
    ChannelInfo(
        track_input=26,
        track_stereo=25,
        track_bformat=24,
        track_channelbus=23,
        track_pfl=15,
    ),
    # Channel 3
    ChannelInfo(
        track_input=30,
        track_stereo=29,
        track_bformat=28,
        track_channelbus=27,
        track_pfl=16,
    ),
    # Channel 4
    ChannelInfo(
        track_input=34,
        track_stereo=33,
        track_bformat=32,
        track_channelbus=31,
        track_pfl=17,
    ),
)

# Setup Mixer behavior
# Measurements in reaper: 
## ReaEQ -26dB: 0.022 
## ReaEQ +6dB: 0.673
## Volume -80dB: 0
## Volume 0dB: 0
## pot centered: 0.5

def slope_constant_power(value):
    resolution = np.arange(start=0, stop=1, step=0.1)
    slope = [0, 0.4, 0.6, 0.70, 0.75, 0.77, 0.80, 0.85, 0.9, 1]
    val = np.interp(value, resolution, slope)
    return val

def slope_eq(value):
    resolution = np.arange(start=0, stop=1, step=0.1)
    slope = [0.22, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.673]
    val = np.interp(value, resolution, slope)
    return val

def slope_fx_freq(value):
    val = np.interp(value, [0, 1], [0.2, 0.8])
    return val

def slope_fx_res(value):
    val = np.interp(value, [0, 1], [0, 1])
    return val

def slope_phones_mix_constant_power(value):
    resolution = np.arange(start=0, stop=1, step=0.1)
    slope = [0, 0.7, 0.85, 0.87, 0.91, 0.93, 0.95, 0.97, 0.99, 1]
    val = np.interp(value, resolution, slope)
    return val

def slope_phones_pfl_constant_power(value):
    resolution = np.arange(start=0, stop=1, step=0.1)
    slope = [1, 0.99, 0.97, 0.95, 0.93, 0.91, 0.87, 0.85, 0.7, 0]
    val = np.interp(value, resolution, slope)
    return val

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
    track_bformat = channel_infos[channel_index].track_bformat
    osc_reaper.send_message(f"/track/{track_bformat}/fx/1/fxparam/8/value", normalized_value)
    #osc_vid.send_message(f"/track/{track_bformat}/fx/1/fxparam/8/value", normalized_value)

def send_width(channel_index):
    elevation_in_radians = channel_infos[channel_index].elevation / 360.0 * 2.0 * math.pi
    # perform narrowing towards zenith, maybe consider sharper falloff
    width = np.interp(channel_infos[channel_index].width * math.cos(elevation_in_radians)
    normalized_value = np.interp(value, [0, 180], [0.5, 0.75])
    track_bformat = channel_infos[channel_index].track_bformat
    osc_reaper.send_message(
        f"/track/{track_bformat}/fx/1/fxparam/10/value", normalized_value)

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

    # A3MIX-POTENTIOMETER

    if parameter == "fx-send":
        val = slope_constant_power(value)
        track_channelbus = channel_infos[channel_index].track_channelbus
        osc_reaper.send_message(f"/track/{track_channelbus}/send/13/volume", val)
        osc_reaper.send_message(f"/track/{track_channelbus}/send/14/volume", val)

    elif parameter == "gain":
        val = slope_constant_power(value)
        osc_reaper.send_message(f"/track/{track_input}/fxeq/gain", val)

    elif parameter == "eq":
        eq_parameter : str = words[4]
        if eq_parameter == "high":
            val = slope_eq(value)
            osc_reaper.send_message(f"/track/{track_input}/fxeq/hishelf/gain", val)

        elif eq_parameter == "mid":
            val = slope_eq(value)
            osc_reaper.send_message(f"/track/{track_input}/fxeq/band/0/gain", val)

        elif eq_parameter == "low":
            val = slope_eq(value)
            osc_reaper.send_message(f"/track/{track_input}/fxeq/loshelf/gain", val)
    
    elif parameter == "volume":
        val = slope_constant_power(value)
        track_channelbus = channel_infos[channel_index].track_channelbus
        osc_reaper.send_message(f"/track/{track_channelbus}/volume", val)


    # A3MIX-BUTTONS

    elif parameter == "pfl" and value == 1:
        channel_infos[channel_index].toggle_pfl = (
            not channel_infos[channel_index].toggle_pfl)
        track_pfl = channel_infos[channel_index].track_pfl
        muted = not channel_infos[channel_index].toggle_pfl
        osc_reaper.send_message(f"/track/{track_pfl}/mute", float(muted))
        osc_mic.send_message(f"/channel/{channel_index}/led/pfl", float(muted))

    elif parameter == "fx" and value == 1:
        channel_infos[channel_index].toggle_fx = (
            not channel_infos[channel_index].toggle_fx)
        is_enabled = channel_infos[channel_index].toggle_fx
        osc_mic.send_message(f"/channel/{channel_index}/led/fx", float(is_enabled))
        set_filters()

    elif parameter == "3d" and value == 1:
        channel_infos[channel_index].toggle_3d = (
            not channel_infos[channel_index].toggle_3d)
        track_stereo = channel_infos[channel_index].track_stereo
        track_bformat = channel_infos[channel_index].track_bformat

        is_enabled = channel_infos[channel_index].toggle_3d
        osc_mic.send_message(f"/channel/{channel_index}/led/3d", float(is_enabled))
        osc_reaper.send_message(
            f"/track/{track_stereo}/mute", float(is_enabled))
        osc_reaper.send_message(
            f"/track/{track_bformat}/mute", float(not is_enabled))

    # A3MOTION

    elif parameter == "azimuth":
        val = np.interp(value, [-180, 180], [0, 1])
        track_bformat = channel_infos[channel_index].track_bformat
        osc_reaper.send_message(f"/track/{track_bformat}/fx/1/fxparam/7/value", val)
        #osc_vid.send_message(f"/track/{track_bformat}/fx/1/fxparam/7/value", val)

    elif parameter == "elevation":
        channel_infos[channel_index].elevation = value
        send_elevation(channel_index)
        send_width(channel_index) # width depends on elevation, narrowing at zenith

    elif parameter == "width":
        channel_infos[channel_index].width = value
        send_width(channel_index)

    elif parameter == "order":
        val = np.interp(value, [0, 3], [0.1, 0.5])
        track_bformat = channel_infos[channel_index].track_bformat
        osc_reaper.send_message(
            f"/track/{track_bformat}/fx/1/fxparam/1/value", val)

def osc_handler_master(address: str,
                       *osc_arguments: List[Any]) -> None:

    #  mypy 0.920 reports a false positive, retest!
    value: float = float(osc_arguments[0])  # type: ignore
    assert type(value) == float

    print(address + " : " + str(value))

    words: List[str] = address.split("/")
    parameter: str = words[2]

    if parameter == "volume":
        val_master = slope_constant_power(value)
        masterbus = master_info.track_masterbus
        master_booth = master_info.track_master_booth
        master_phones = master_info.track_master_phones
        master_rec = master_info.track_master_rec
        osc_reaper.send_message(f"/track/{masterbus}/volume", val_master)
        osc_reaper.send_message(f"/track/{master_booth}/volume", val_master)
        osc_reaper.send_message(f"/track/{master_phones}/volume", val_master)
        osc_reaper.send_message(f"/track/{master_rec}/volume", val_master)

    if parameter == "booth":
        val = slope_constant_power(value)
        track = master_info.track_booth
        osc_reaper.send_message(f"/track/{track}/volume", val)

    if parameter == "phones_mix":
        phones_mix = slope_phones_mix_constant_power(value)
        phones_pfl = slope_phones_pfl_constant_power(value)
        track_mainmixbus = master_info.track_mainmixbus
        osc_reaper.send_message(f"/track/{track_mainmixbus}/volume", phones_mix)
        for channel_index in range(4):
            track_pfl = channel_infos[channel_index].track_pfl
            osc_reaper.send_message(f"/track/{track_pfl}/volume", phones_pfl)

    if parameter == "phones_volume":
        val = slope_constant_power(value)
        track_phones = master_info.track_phones
        osc_reaper.send_message(f"/track/{track_phones}/volume", val)

    elif parameter == "return":
        val = slope_constant_power(value)
        track_reverb_binaural = master_info.track_reverb_binaural
        track_reverb_stereo = master_info.track_reverb_stereo
        osc_reaper.send_message(f"/track/{track_reverb_binaural}/volume", val)
        osc_reaper.send_message(f"/track/{track_reverb_stereo}/volume", val)

def osc_handler_fx(address: str,
                   *osc_arguments: List[Any]) -> None:

    value = osc_arguments[0]

    print(address + " : " + str(value))

    words: List[str] = address.split("/")
    parameter: str = words[2]

    if parameter == "mode":
        high_pass = value == "high_pass"
        master_info.fx_mode = MasterInfo.FXMode.HIGH_PASS if high_pass else MasterInfo.FXMode.LOW_PASS
        osc_mic.send_message("/fx/led", "high_pass" if high_pass else "low_pass")
        set_filters()

    elif parameter == "frequency":
        val = slope_fx_freq(value)
        for channel_index in range(4):
            track_input = channel_infos[channel_index].track_input
            osc_reaper.send_message(f"/track/{track_input}/fx/{FX_INDEX_HIPASS}/fxparam/7/value", val)
            osc_reaper.send_message(f"/track/{track_input}/fx/{FX_INDEX_LOPASS}/fxparam/7/value", val)

    elif parameter == "resonance":
        val = slope_fx_res(value)
        for channel_index in range(4):
            track_input = channel_infos[channel_index].track_input
            osc_reaper.send_message(f"/track/{track_input}/fx/{FX_INDEX_HIPASS}/fxparam/6/value", val)
            osc_reaper.send_message(f"/track/{track_input}/fx/{FX_INDEX_LOPASS}/fxparam/6/value", val)

def osc_handler_tap(address: str,
                   *osc_arguments: List[Any]) -> None:

    value = osc_arguments[0]

    print(address + " : " + str(value))

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
    dispatcher.map("/tap", osc_handler_tap)

    # # Motion-Controller
    # # dispatcher.map("/CoordinateConverter/*", iemToCtrlMotion_handler)
    # # dispatcher.map("/moc/channel/*", ctrlMotionToIem_handler)
    # # dispatcher.map("/moc/channel/*", ctrlMotionToIem_handler)

    server = osc_server.ThreadingOSCUDPServer((args.ip, args.port), dispatcher)
    print("Serving on {}".format(server.server_address))
    server.serve_forever()


# def vu_handler(address: str, *osc_arguments: List[Any]) -> None:

#     words = address.split("/")
#     section: str = words[3]

#     #  mypy 0.920 reports a false positive, retest!
#     value: float = float(osc_arguments[0])  # type: ignore
#     assert type(value) == float

#     fp = [0, 0.05, 0.10, 0.15, 0.20, 0.40, 0.60, 0.65, 0.75, 0.90]
#     xp = [0, 0.25, 0.30, 0.37, 0.43, 0.50, 0.55, 0.58, 0.60, 0.64]
#     val = np.interp(value, xp, fp)
#     osc_mic.send_message(f"/track/{section}/vu", val)
#     # print(str(value))


# def ctrlMotionToIem_handler(address: str,
#                             *osc_arguments: List[Any]) -> None:
#     words = address.split("/")
#     track = words[3]
#     param = words[4]

#     # print(words)
#     # value = osc_arguments
#     # print("/ctrlMotion/track/" + track + "/" + param + "/ : " + str(value))

#     if track == "1":
#         if param == "xyz":
#             iem_1.send_message("/CoordinateConverter/xPos",
#                                np.interp(osc_arguments[1], [0, 1], [-1, 1]))
#             iem_1.send_message("/CoordinateConverter/yPos",
#                                np.interp(osc_arguments[0], [0, 1], [1, -1]))

#     if track == "2":
#         match_xyz = re.match(param, "xyz")
#         if match_xyz:
#             iem_2.send_message("/CoordinateConverter/xPos",
#                                np.interp(osc_arguments[1], [0, 1], [-1, 1]))
#             iem_2.send_message("/CoordinateConverter/yPos",
#                                np.interp(osc_arguments[0], [0, 1], [1, -1]))
#             # iem_2.send_message("/CoordinateConverter/zPos", osc_arguments[2])
#         if param == "width":
#             iem_2.send_message("/CoordinateConverter/radius", osc_arguments[0])
#         if param == "side":
#             osc_reaper.send_message("/track/" + dj2_in + "/fx/2/fxparam/1/value",
#                                 osc_arguments[0])

#     if track == "3":
#         match_xyz = re.match(param, "xyz")
#         if match_xyz:
#             iem_3.send_message("/CoordinateConverter/xPos",
#                                np.interp(osc_arguments[1], [0, 1], [-1, 1]))
#             iem_3.send_message("/CoordinateConverter/yPos",
#                                np.interp(osc_arguments[0], [0, 1], [1, -1]))
#             # iem_3.send_message("/CoordinateConverter/zPos", osc_arguments[2])
#         if param == "width":
#             iem_3.send_message("/CoordinateConverter/radius", osc_arguments[0])
#         if param == "side":
#             osc_reaper.send_message("/track/" + dj3_in + "/fx/2/fxparam/1/value",
#                                 osc_arguments[0])

#     if track == "4":
#         match_xyz = re.match(param, "xyz")
#         if match_xyz:
#             iem_4.send_message("/CoordinateConverter/xPos",
#                                np.interp(osc_arguments[1], [0, 1], [-1, 1]))
#             iem_4.send_message("/CoordinateConverter/yPos",
#                                np.interp(osc_arguments[0], [0, 1], [1, -1]))
#             # iem_4.send_message("/CoordinateConverter/zPos", osc_arguments[2])
#         if param == "width":
#             iem_4.send_message("/CoordinateConverter/radius", osc_arguments[0])
#         if param == "side":
#             osc_reaper.send_message("/track/" + dj4_in + "/fx/2/fxparam/1/value",
#                                 osc_arguments[0])


# def iemToCtrlMotion_handler(address: str,
#                             *osc_arguments: List[Any]) -> None:
#     words = address.split("/")
#     track = words[1]
#     param = words[2]

#     print(words)
#     # value = osc_arguments[0]
#     # print("/CoordinateConverter/" + track + "/" + param + " : " + str(value))

#     if track == "1":
#         if (param == "xPos" or param == "yPos" or param == "yPos"):
#             if param == "xPos":
#                 val_send_ch1_xyz[1] = (np.interp(
#                     osc_arguments[0], [-1, 1], [0, 1]))
#             if param == "yPos":
#                 val_send_ch1_xyz[0] = (np.interp(
#                     osc_arguments[0], [-1, 1], [1, 0]))
#             else:
#                 val_send_ch1_xyz[2] = (np.interp(
#                     osc_arguments[0], [-1, 1], [0, 1]))
#             osc_moc.send_message(
#                 "/moc/channel/1/pos/xyz", val_send_ch1_xyz)

#         if param == "radius":
#             osc_moc.send_message(
#                 "/ctrlMotion/track/1/width", osc_arguments[0])

#     if track == "2":
#         if (param == "xPos" or param == "yPos" or param == "yPos"):
#             if param == "xPos":
#                 val_send_ch2_xyz[1] = (np.interp(
#                     osc_arguments[0], [-1, 1], [0, 1]))
#             if param == "yPos":
#                 val_send_ch2_xyz[0] = (np.interp(
#                     osc_arguments[0], [-1, 1], [1, 0]))
#             else:
#                 val_send_ch2_xyz[2] = (np.interp(
#                     osc_arguments[0], [-1, 1], [0, 1]))
#             osc_moc.send_message(
#                 "/moc/channel/2/pos/xyz", val_send_ch2_xyz)

#         if param == "radius":
#             osc_moc.send_message(
#                 "/ctrlMotion/track/2/width", osc_arguments[0])

#     if track == "3":
#         if (param == "xPos" or param == "yPos" or param == "yPos"):
#             if param == "xPos":
#                 val_send_ch3_xyz[1] = (np.interp(
#                     osc_arguments[0], [-1, 1], [0, 1]))
#             if param == "yPos":
#                 val_send_ch3_xyz[0] = (np.interp(
#                     osc_arguments[0], [-1, 1], [1, 0]))
#             else:
#                 val_send_ch3_xyz[2] = (np.interp(
#                     osc_arguments[0], [-1, 1], [0, 1]))
#             osc_moc.send_message(
#                 "/moc/channel/3/pos/xyz", val_send_ch3_xyz)

#         if param == "radius":
#             osc_moc.send_message(
#                 "/ctrlMotion/track/3/width", osc_arguments[0])

#     if track == "4":
#         if (param == "xPos" or param == "yPos" or param == "yPos"):
#             if param == "xPos":
#                 val_send_ch4_xyz[1] = (np.interp(
#                     osc_arguments[0], [-1, 1], [0, 1]))
#             if param == "yPos":
#                 val_send_ch4_xyz[0] = (np.interp(
#                     osc_arguments[0], [-1, 1], [1, 0]))
#             else:
#                 val_send_ch4_xyz[2] = (np.interp(
#                     osc_arguments[0], [-1, 1], [0, 1]))
#             osc_moc.send_message(
#                 "/moc/channel/4/pos/xyz", val_send_ch4_xyz)

#         if param == "radius":
#             osc_moc.send_message(
#                 "/ctrlMotion/track/4/width", osc_arguments[0])


