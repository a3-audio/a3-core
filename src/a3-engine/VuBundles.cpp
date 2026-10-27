// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "VuBundles.h"
#include <algorithm>
#include <cstring>
#include "VuTaps.h"

namespace a3
{
namespace
{
constexpr int bundleSlot = 512;   // the analyzer's queue slot (OscSender::Packet::data)

void appendInt32 (std::vector<uint8_t>& out, uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back ((uint8_t) ((value >> shift) & 0xff));
}

void appendFloat32 (std::vector<uint8_t>& out, float value)
{
    uint32_t bits = 0;
    std::memcpy (&bits, &value, sizeof bits);
    appendInt32 (out, bits);
}

// An OSC string: its bytes, then 1 to 4 zeros up to a multiple of 4.
void appendOscString (std::vector<uint8_t>& out, const std::string& text)
{
    out.insert (out.end(), text.begin(), text.end());
    const size_t padded = (text.size() + 1 + 3) & ~(size_t) 3;
    out.insert (out.end(), padded - text.size(), 0);
}
}

std::string vuAddress (const std::string& pattern, int slot)
{
    std::string address = pattern;
    const auto placeholder = address.find ("{n}");
    if (placeholder != std::string::npos)
        address.replace (placeholder, 3, std::to_string (VuTaps::vuNumber (slot)));
    return address;
}

std::vector<std::pair<int, int>> vuBundleChunks (int numChannels)
{
    std::vector<std::pair<int, int>> chunks;
    for (int first = 0; first < numChannels;)
    {
        const int block = first < VuTaps::mapChannels ? VuTaps::mapBlock : VuTaps::stereoBlock;
        const int count = std::min (block, numChannels - first);
        chunks.emplace_back (first, count);
        first += count;
    }
    return chunks;
}

std::vector<uint8_t> serializeVuBundle (const std::string* addresses, const float* peaks, const float* rms, int count)
{
    std::vector<uint8_t> bundle;
    appendOscString (bundle, "#bundle");
    appendInt32 (bundle, 0);   // timetag seconds
    appendInt32 (bundle, 1);   // fraction: 1 = immediately
    for (int i = 0; i < count; ++i)
    {
        if ((int) bundle.size() + 4 >= bundleSlot - 32)   // OscSender::serializeBundle's safety
            break;
        std::vector<uint8_t> message;
        appendOscString (message, addresses[i]);
        appendOscString (message, ",ff");
        appendFloat32 (message, peaks[i]);
        appendFloat32 (message, rms[i]);
        appendInt32 (bundle, (uint32_t) message.size());
        bundle.insert (bundle.end(), message.begin(), message.end());
    }
    return bundle;
}

std::vector<std::vector<uint8_t>> vuBundles (const std::string& pattern, const std::vector<float>& peaks,
                                             const std::vector<float>& rms)
{
    const int channels = (int) std::min (peaks.size(), rms.size());
    std::vector<std::string> addresses;
    for (int slot = 0; slot < channels; ++slot)
        addresses.push_back (vuAddress (pattern, slot));
    std::vector<std::vector<uint8_t>> bundles;
    for (const auto& [first, count] : vuBundleChunks (channels))
        bundles.push_back (serializeVuBundle (addresses.data() + first, peaks.data() + first, rms.data() + first, count));
    return bundles;
}
}
