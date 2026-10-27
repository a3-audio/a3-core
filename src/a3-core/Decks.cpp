// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Decks.h"

#include <algorithm>

namespace a3core
{
namespace
{
// The evening's name for the block (a file key, not an OSC address).
const juce::Identifier eveningKey ("channel.source");

std::pair<std::string, OscArg> threeD (const Truth& truth, int deck, double value)
{
    return { truth.address ("stemdeck.3d", { { "deck", deck } }), pyFloat (value) };
}

int wrapped (int value, int size) { return ((value % size) + size) % size; }
}

int deckPort (const Truth& truth, int deck) { return truth.endpoint ("stemdeck", "osc").port + deck - 1; }

Endpoint deckEndpoint (const Truth& truth, const std::string& host, int deck) { return { host, deckPort (truth, deck) }; }

DeckSelector::DeckSelector (int channels, int decks)
    : channelCount (channels), deckCount (decks), sources (static_cast<size_t> (channels), analog)
{
    for (int channel = 1; channel <= channelCount; ++channel)
    {
        const auto positions = ring (channel);
        cursors.push_back (static_cast<int> (std::find (positions.begin(), positions.end(), analog) - positions.begin()));
    }
}

std::vector<int> DeckSelector::ring (int) const
{
    std::vector<int> positions;
    for (int deck = 1; deck <= deckCount; ++deck)
        positions.push_back (deck);
    positions.push_back (analog);
    return positions;
}

int DeckSelector::cursor (int channel) const { return cursors[static_cast<size_t> (channel - 1)]; }

void DeckSelector::turn (int channel, int steps)
{
    auto& place = cursors[static_cast<size_t> (channel - 1)];
    place = wrapped (place + steps, static_cast<int> (ring (channel).size()));
}

std::vector<std::pair<int, int>> DeckSelector::push (int channel)
{
    const auto chosen = ring (channel)[static_cast<size_t> (cursor (channel))];
    if (chosen == source (channel))
        return {};
    heldBefore = held();
    std::vector<std::pair<int, int>> changes { { channel, chosen } };
    if (chosen != analog)
        if (const auto other = channelOf (chosen))
            changes.push_back ({ *other, analog });
    for (const auto& [changed, to] : changes)
        set (changed, to);
    return changes;
}

void DeckSelector::set (int channel, int newSource)
{
    sources[static_cast<size_t> (channel - 1)] = newSource;
    const auto positions = ring (channel);
    cursors[static_cast<size_t> (channel - 1)] = static_cast<int> (std::find (positions.begin(), positions.end(), newSource) - positions.begin());
}

int DeckSelector::source (int channel) const { return sources[static_cast<size_t> (channel - 1)]; }

bool DeckSelector::deckChannel (int channel) const { return source (channel) != analog; }

std::optional<int> DeckSelector::channelOf (int deck) const
{
    for (int channel = 1; channel <= channelCount; ++channel)
        if (source (channel) == deck)
            return channel;
    return std::nullopt;
}

void DeckSelector::heard (int deck, double now) { lastHeard[deck] = now; }

std::optional<double> DeckSelector::lastHeardOf (int deck) const
{
    const auto found = lastHeard.find (deck);
    return found == lastHeard.end() ? std::nullopt : std::optional<double> (found->second);
}

bool DeckSelector::present (int deck, double now) const
{
    const auto found = lastHeard.find (deck);
    return found != lastHeard.end() && now - found->second <= presenceSeconds;
}

std::vector<std::pair<std::string, OscArg>> DeckSelector::threeDMessages (const Truth& truth, int channel, double value) const
{
    const auto deck = source (channel);
    if (deck == analog)
        return {};
    return { threeD (truth, deck, value) };
}

std::vector<std::pair<std::string, OscArg>> DeckSelector::leaveMessages (const Truth& truth, const std::vector<std::pair<int, int>>& changes) const
{
    if (changes.empty())
        return {};
    const auto now = held();
    std::vector<std::pair<std::string, OscArg>> out;
    for (int deck : heldBefore)   // a std::set walks in ascending order, as sorted() does
        if (! now.count (deck))
            out.push_back (threeD (truth, deck, neutralThreeD));
    return out;
}

std::set<int> DeckSelector::held() const
{
    std::set<int> decksHeld;
    for (int source : sources)
        if (source != analog)
            decksHeld.insert (source);
    return decksHeld;
}

std::vector<std::pair<std::string, OscArg>> DeckSelector::helloMessages (const Truth& truth, int deck, double channelThreeD) const
{
    return { threeD (truth, deck, channelOf (deck) ? channelThreeD : neutralThreeD) };
}

juce::var DeckSelector::toEvening() const
{
    juce::Array<juce::var> listed;
    for (int source : sources)
        listed.add (source);
    auto* object = new juce::DynamicObject();
    object->setProperty (eveningKey, juce::var (listed));
    return juce::var (object);
}

void DeckSelector::fromEvening (const juce::var& data)
{
    const auto* listed = data.getDynamicObject() != nullptr ? data[eveningKey].getArray() : nullptr;
    if (listed == nullptr || listed->size() != channelCount)
        return;
    std::set<int> taken;
    for (int channel = 1; channel <= channelCount; ++channel)
    {
        const auto& item = (*listed)[channel - 1];
        int chosen = analog;
        if (! item.isBool() && (item.isInt() || item.isInt64()))
        {
            const auto whole = static_cast<juce::int64> (item);
            if (whole >= 1 && whole <= deckCount && ! taken.count (static_cast<int> (whole)))
                chosen = static_cast<int> (whole);
        }
        taken.insert (chosen);
        set (channel, chosen);
    }
}
}
