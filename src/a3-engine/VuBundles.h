// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The /vu datagrams exactly as beat-analyzer sends them (7dd100b, OscSender::vuBundleChunks and
// serializeBundle, Audio::vuOscPath): one OSC bundle per block, "#bundle", timetag 0/1 (immediately),
// then per meter a big-endian length and the message "<address>" ",ff" peak, rms (float32 big-endian).
namespace a3
{
// The truth's vu pattern ("{n}" replaced) for slot i of VuTaps: /vu/(i+1) for the forty, /vu/(i+11) after.
std::string vuAddress (const std::string& pattern, int slot);

// Which slots each bundle carries, (first, count) in order: blocks of 10 below 40, of 8 from there.
std::vector<std::pair<int, int>> vuBundleChunks (int numChannels);

// One bundle of `count` messages; stops adding messages, as the analyzer, where fewer than 32 bytes of
// its 512-byte slot would remain.
std::vector<uint8_t> serializeVuBundle (const std::string* addresses, const float* peaks, const float* rms, int count);

// Every bundle of one send: peaks and rms per slot (VuTaps::take), 56 entries in /vu order.
std::vector<std::vector<uint8_t>> vuBundles (const std::string& pattern, const std::vector<float>& peaks,
                                             const std::vector<float>& rms);
}
