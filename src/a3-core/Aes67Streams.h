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
    // What the sender announces (node.channel-names; its JACK ports are send_<name>):
    // "SUB", "T1".."Tn" for a zone, "L", "R" for a pair, "CH1".. for a fixed stream.
    std::vector<std::string> channelNames;
    // The engine outputs that feed it, in channelNames' order ("main_sub", "main_top_1", "rec_L", ...);
    // empty for a fixed stream, whose links are not the engine's.
    std::vector<std::string> enginePorts;
};

// The table's Core rows in table order ({host} in a session is filled in). Without a table: none. A row with a
// port outside 1-65535, a ptime not above 0, a fixed row without channels or no group is left out and said in
// `problems`. Throws TruthError for a row the reader cannot resolve (an unknown source, a group that is neither an
// address nor a hosts key).
// A row whose packet would not fit one frame (channels * 3 * round(ptime * 44.1) + 12 > 1472 bytes) is left out
// alone, and `problems` (when given) gains a line naming it and the limit.
std::vector<StreamSpec> resolveTxStreams (const Truth&, const a3::SpatialConfig&, const std::string& hostname,
                                          std::vector<std::string>* problems = nullptr);

// The Core rows that need no speaker layout (every source but zone:*: the pairs and fixed rows), resolved as
// resolveTxStreams resolves them, for a caller that cannot read the layout. A zone row is never resolved here.
std::vector<StreamSpec> layoutFreeTxStreams (const Truth&, const std::string& hostname, std::vector<std::string>* problems = nullptr);

// Changes iff a stream's session, group, port, ptime or channel names change.
std::string streamsSignature (const std::vector<StreamSpec>&);

// True unless `renderedSignature` is the streams' signature: what a rendered config announces is out of date.
bool streamsChanged (const std::string& renderedSignature, const std::vector<StreamSpec>&);
}
