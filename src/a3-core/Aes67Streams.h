// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "SpatialConfig.h"
#include "Truth.h"

namespace a3core
{
// One AES67 stream the Core sends, resolved against the live layout.
struct StreamSpec
{
    std::string key, session, group;   // group as dotted address
    int port = 5004;
    double ptimeMs = 0.5;
    bool legacy = false;
    // What the sender announces (node.channel-names; its JACK ports are send_<name>):
    // "SUB", "T1".."Tn" for a zone, "L", "R" for a pair, "CH1".. for a fixed stream.
    std::vector<std::string> channelNames;
    // The engine outputs that feed it, in channelNames' order ("main_sub", "main_top_1", "rec_L", ...);
    // empty for a fixed stream (the legacy one), whose links are not the engine's.
    std::vector<std::string> enginePorts;
};

// The legacy stream first (session "AES67 <hostname> out", group hosts["aes67-core"], CH1..CH12), then the
// table's other Core rows in table order. Without a table: the legacy stream only. Throws TruthError for a
// row the reader cannot resolve (an unknown source, a group that is neither an address nor a hosts key).
std::vector<StreamSpec> resolveTxStreams (const Truth&, const a3::SpatialConfig&, const std::string& hostname);

// Changes iff a stream's session, group, port, ptime or channel names change.
std::string streamsSignature (const std::vector<StreamSpec>&);
}
