// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cassert>
#include <memory>
#include "ChannelStrip.h"
#include "a3-delay/DelayParameters.h"
#include "DjFilter.h"

namespace a3
{
// Wire values, written by the OSC thread and read by the engine once per chunk.
struct ChannelControls : StripControls
{
    std::atomic<int> source { 0 };   // 0 = analog, k = deck slot k
    std::atomic<float> cue { 0.0f }, auxSend { 0.0f };
    std::atomic<float> azimuth { 0.0f }, elevation { 0.0f };   // degrees; only an analog path uses them
};

struct GlobalControls
{
    FilterControls filter;
    std::atomic<float> masterVolume { 0.0f }, booth { 0.0f }, phonesMix { 0.0f }, phonesVolume { 0.0f }, auxReturn { 0.0f };
    std::atomic<float> returnCue { 0.0f }, delayBpm { 120.0f };
    // The AUX-RTN delay page, as the engine's words store them (snapped and clamped).
    std::atomic<float> delayBeat { delay::defaultBeat }, delayFeedback { delay::defaultFeedback };
    std::atomic<float> delayColor { delay::defaultColor }, delaySpread { delay::defaultSpread };
    std::atomic<bool> gateOpen { false };
};

class EngineControls
{
public:
    EngineControls (int numChannels, int numDecks)
        : count (numChannels > 0 ? numChannels : 0), perChannel (std::make_unique<ChannelControls[]> ((size_t) count)),
          deckCount (numDecks > 0 ? numDecks : 0), stereoDecks (std::make_unique<std::atomic<bool>[]> ((size_t) deckCount))
    {
        for (int i = 0; i < deckCount; ++i)
            stereoDecks[(size_t) i].store (false);
    }

    int channels() const { return count; }
    ChannelControls& channel (int index)
    {
        assert (index >= 0 && index < count);
        return perChannel[(size_t) index];
    }
    GlobalControls& global() { return globals; }
    const GlobalControls& global() const { return globals; }

    // Deck slot 1..decks sends one stereo pair (StemDeck's Output: Stereo), written by the control thread
    // from the links (deckModes); a slot outside the range is never stereo.
    int decks() const { return deckCount; }
    void setDeckStereo (int slot, bool stereo)
    {
        if (slot >= 1 && slot <= deckCount)
            stereoDecks[(size_t) slot - 1].store (stereo, std::memory_order_relaxed);
    }
    bool isDeckStereo (int slot) const
    {
        return slot >= 1 && slot <= deckCount && stereoDecks[(size_t) slot - 1].load (std::memory_order_relaxed);
    }

private:
    int count;
    std::unique_ptr<ChannelControls[]> perChannel;
    int deckCount;
    std::unique_ptr<std::atomic<bool>[]> stereoDecks;
    GlobalControls globals;
};
}
