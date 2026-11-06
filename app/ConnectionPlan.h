// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ConfigFile.h"
#include "Aes67Streams.h"
#include "a3-links/LinkMap.h"
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

namespace a3app
{
// Spec 4b: with connecting on, a client named REAPER beside the engine would put a second
// engine on the PA through the default map; refuse. --no-connect makes no link and runs.
juce::Result refuseBesideReaper (const std::vector<std::string>& clients, bool connecting);

// P40: rec_L/R -> "AES67 <hostname> out:send_CH1/2" (the name a3-osc-render gives this machine's sender),
// added to whatever map was loaded, so a deck host records the REC bus even from an engine.json saved
// before M10. Applied at link time only: the user's file is never rewritten. No host name, no link.
// With `legacy` (the legacy stream as resolved) the sender is named after its session instead.
void addRecToAes67Sender (a3::links::LinkMap&, const std::string& hostname, const a3core::StreamSpec* legacy = nullptr);

// The Core's other AES67 senders: for every non-legacy stream and channel k (1-based) the engine output
// enginePorts[k-1] gains the peer "<session>:send_<channelNames[k-1]>" (the sender node is named after its
// session; its JACK ports after node.channel-names). The legacy stream is skipped: its link is the one above.
void addStreamSenders (a3::links::LinkMap&, const std::vector<a3core::StreamSpec>&);

// rec_L -> beat-analyzer:bpm_1, the beat clock's input, as the patchbay wired it. Built in like the sender link
// (2026-10-30): the REC row of the I/O map stays free for an analog recorder, and the analyzer is always linked
// while its port exists. An old file's REC entry naming the analyzer is dropped at load (ConfigFile.h).
inline constexpr const char* beatAnalyzerInput = "beat-analyzer:bpm_1";
void addBeatAnalyzerLink (a3::links::LinkMap&);

// The loop's own ports to the loop's channels: roundtrip_out -> system:playback_<output>, system:capture_<input>
// -> roundtrip_in (the device's JACK names; RoundTripLoop.h), and likewise analogtest_out/in to a chosen analog test
// pair (both sides chosen). Nothing without a loop. Built in like the sender link above: never written to the file.
void addRoundTripLoopLinks (IoMap&);
bool hasAnalogTestPair (const IoMap&);   // an analog loop with both sides chosen

// What the engine links and counts: the loaded io map plus the built-in links above. `hostname` is
// Platform::hostname(), the source a3-osc-render names the sender from. `streams`: the Core's resolved
// transmit streams (empty: only the legacy sender link above).
IoMap linkMapFor (const AppConfig&, const std::string& hostname, const std::vector<a3core::StreamSpec>& streams = {});

// Whether JackBackend::service applies the map now: at once when a port registered or went
// (a deck host booting after the Core, a3-aes67 restarting), and once a second in case a
// registration callback was missed. Never with --no-connect.
constexpr double linkEverySeconds = 1.0;
bool linkPlanDue (bool connecting, bool portsChanged, double nowSeconds, double lastLinkSeconds);
}
