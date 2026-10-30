// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Which device "system:playback_N" / "system:capture_N" is. On PipeWire (jack.default-as-system) they are the
// default sink and source of the moment, whatever node that is: during a WirePlumber restart on 2026-10-30 they
// were the AES67 receiver "AES67 deck C" and the AES67 sender, and the engine linked those to analog 1-8 and the
// main outputs. So the engine links to system only while it is the configured device, side by side, and to nothing
// while it is not. Control thread only (the watch runs pw-metadata).
#pragma once
#include "CommandRunner.h"
#include "IoMap.h"
#include <string>
#include <vector>

namespace a3app
{
// `pw-metadata -n default 0`: default.audio.sink/source (what PipeWire uses now) and
// default.configured.audio.sink/source (the user's choice, kept while the device is gone); empty when not listed.
struct DefaultNodes
{
    std::string sink, source, configuredSink, configuredSource;
};
Command defaultNodesCommand();
DefaultNodes parseDefaultNodes (const std::string& pwMetadataOutput);

// A name for the I/O page: "Scarlett 18i20 USB" from "alsa_output.usb-Focusrite_Scarlett_18i20_USB_<serial>-00.pro-output-0"
// (bus, vendor, serial and profile left out); "Device" when the node says nothing better. The DefaultNodes form
// names the configured sink, else the current one.
juce::String deviceNameOf (const std::string& alsaNode);
juce::String deviceNameOf (const DefaultNodes&);

// Whether "system" is the engine's device on each side: the default is the configured node, or, with nothing
// configured (a fresh machine), an ALSA node ("alsa_…"; never a stream such as AES67).
struct DevicePresence
{
    bool playback = false, capture = false;
    bool operator== (const DevicePresence& o) const { return playback == o.playback && capture == o.capture; }
};
DevicePresence devicePresenceOf (const DefaultNodes&);

// The map without its system:playback_* peers while playback is absent, and without system:capture_* while capture
// is (direct links with such an end too). `dropped` gets those peers, each once, in the map's order.
IoMap withoutAbsentDevice (const IoMap&, DevicePresence, std::vector<std::string>& dropped);

// Reads the presence once per link pass. Until pw-metadata has answered once, both sides count as present: without
// PipeWire, system is the JACK server's own device (jackd). After that, a failed read keeps the last answer.
class SystemDeviceWatch
{
public:
    explicit SystemDeviceWatch (CommandRunner&);
    DevicePresence read();
    const DefaultNodes& nodes() const { return lastNodes; }   // the last answer (empty before one)

private:
    CommandRunner& runner;
    DevicePresence last { true, true };
    DefaultNodes lastNodes;
};
}
