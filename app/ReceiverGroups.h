// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EngineConfig.h"
#include <string>
#include <vector>

namespace a3app
{
// A named set of the engine's existing input ports that an AES67 receiver patch feeds.
// Nothing here adds a port; it only gives the ports a name a patch can be made against.
struct ReceiverGroup
{
    std::string name;
    std::vector<std::string> ports;
    // analog_N is also fed from the sound card's capture ports by the I/O map.
    bool sharedWithSoundCard = false;
};

// Per deck slot: "deck X B-format", "deck X return B-format" (feeds the delay); then each
// complete analog pair "analog 1-2", ...; then "return stereo" (analog_11, analog_12) when
// the config has at least 12 analog inputs.
std::vector<ReceiverGroup> receiverGroups (const a3::EngineConfig&);
}
