// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Stems.h"

#include <algorithm>

namespace a3core
{
namespace
{
constexpr int allBuses = (1 << Stems::aux) - 1;
// The width a report may have: a StemDeck from before 2026-10-07 still had a sixth bus, its CUE.
constexpr int reportBuses = (1 << (Stems::aux + 1)) - 1;

int bitOf (int bus) { return 1 << (bus - 1); }

// Python's %: the result takes the divisor's sign.
int wrapped (int value, int size) { return ((value % size) + size) % size; }

template <typename Range>
int indexIn (const Range& range, int value)
{
    return static_cast<int> (std::find (range.begin(), range.end(), value) - range.begin());
}

template <typename Range>
bool contains (const Range& range, int value) { return std::find (range.begin(), range.end(), value) != range.end(); }

// Python's _is_int: a whole JSON number, not a bool and not a float.
std::optional<int> wholeNumber (const juce::var& value)
{
    if (value.isBool() || ! (value.isInt() || value.isInt64()))
        return std::nullopt;
    const auto whole = static_cast<juce::int64> (value);
    if (whole < -1000000 || whole > 1000000)
        return std::nullopt;
    return static_cast<int> (whole);
}

std::optional<std::array<std::optional<int>, Stems::channels>> sourcesOf (const juce::var& data)
{
    const auto* listed = data.getArray();
    if (listed == nullptr || listed->size() != Stems::channels)
        return std::nullopt;
    std::array<std::optional<int>, Stems::channels> sources {};
    for (int i = 0; i < Stems::channels; ++i)
    {
        const auto& item = (*listed)[i];
        if (item.isVoid())
            continue;
        const auto whole = wholeNumber (item);
        if (! whole || (*whole != Stems::analogSource && *whole != Stems::stemSource))
            return std::nullopt;
        sources[static_cast<size_t> (i)] = whole;
    }
    return sources;
}

std::optional<std::array<int, Stems::channels>> cursorsOf (const juce::var& data)
{
    const auto* listed = data.getArray();
    if (listed == nullptr || listed->size() != Stems::channels)
        return std::nullopt;
    std::array<int, Stems::channels> cursors {};
    for (int i = 0; i < Stems::channels; ++i)
    {
        const auto whole = wholeNumber ((*listed)[i]);
        if (! whole || *whole < 0 || *whole > Stems::analogInput)
            return std::nullopt;
        cursors[static_cast<size_t> (i)] = *whole;
    }
    return cursors;
}
}

std::array<int, 3> ring (int index) { return { index, Stems::stemsPerDeck + index, Stems::analogInput }; }

int ownChannel (int pair) { return (pair - 1) % Stems::stemsPerDeck; }

bool Stems::report (int pair, int mask)
{
    if (pair < 1 || pair > pairs || mask < 0 || mask > reportBuses)
        return false;
    masks[static_cast<size_t> (pair - 1)] = mask & allBuses;
    return true;
}

void Stems::forget() { masks.fill (0); }

bool Stems::isOn (int pair, int bus) const { return (masks[static_cast<size_t> (pair - 1)] & bitOf (bus)) != 0; }

int Stems::channelMask (int index) const
{
    int mask = 0;
    for (int pair = 1; pair <= pairs; ++pair)
        if (isOn (pair, index + 1))
            mask += 1 << (pair - 1);
    return mask;
}

bool Stems::playsOnReturn (int pair) const { return isOn (pair, aux); }

std::optional<int> Stems::placeOf (int pair) const
{
    for (int index = 0; index < channels; ++index)
        if (isOn (pair, index + 1))
            return index;
    if (isOn (pair, aux))
        return returnIndex;
    return std::nullopt;
}

void Stems::adoptSources()
{
    for (int index = 0; index < channels; ++index)
    {
        auto& source = sources[static_cast<size_t> (index)];
        if (! source)
            source = channelMask (index) ? stemSource : analogSource;
    }
}

void Stems::turn (int index, int steps)
{
    const auto positions = ring (index);
    auto& cursor = cursors[static_cast<size_t> (index)];
    const auto place = contains (positions, cursor) ? indexIn (positions, cursor) : indexIn (positions, analogInput);
    cursor = positions[static_cast<size_t> (wrapped (place + steps, static_cast<int> (positions.size())))];
}

std::vector<Command> Stems::push (int index, bool connected)
{
    const auto cursor = cursors[static_cast<size_t> (index)];
    if (! contains (ring (index), cursor))
        return {};
    if (cursor == analogInput)
    {
        sources[static_cast<size_t> (index)] = analogSource;
        return {};
    }
    if (! connected)
        return {};
    sources[static_cast<size_t> (index)] = stemSource;
    return select (index, cursor + 1);
}

std::vector<Command> Stems::select (int index, int pair)
{
    const auto bus = index + 1;
    if (ownChannel (pair) != index || isOn (pair, bus))
        return {};
    std::vector<int> displaced;
    for (int p = 1; p <= pairs; ++p)
        if (p != pair && isOn (p, bus))
            displaced.push_back (p);
    std::vector<Command> commands { { pair, bus, true }, { pair, aux, false } };
    for (int p : displaced)
        commands.push_back ({ p, bus, false });
    applyAll (commands);
    for (int p : displaced)
        if (! placeOf (p))
            for (const auto& command : toAux (p))
                commands.push_back (command);
    for (const auto& command : tidy())
        commands.push_back (command);
    return commands;
}

std::vector<Command> Stems::tidy()
{
    std::array<std::optional<int>, pairs> before;
    for (int pair = 1; pair <= pairs; ++pair)
        before[static_cast<size_t> (pair - 1)] = placeOf (pair);

    std::vector<Command> commands;
    for (int pair = 1; pair <= pairs; ++pair)
        for (int c = 0; c < channels; ++c)
            if (c != ownChannel (pair) && isOn (pair, c + 1))
                commands.push_back ({ pair, c + 1, false });
    applyAll (commands);
    for (int index = 0; index < channels; ++index)
    {
        const auto bus = index + 1;
        bool first = true;
        for (int pair = 1; pair <= pairs; ++pair)
        {
            if (! isOn (pair, bus))
                continue;
            if (! first)
                commands.push_back ({ pair, bus, false });
            first = false;
        }
    }
    // Applied before the next rule reads the mirror: a stem taken off one bus here may be alone, and right, on another.
    applyAll (commands);
    for (int pair = 1; pair <= pairs; ++pair)
    {
        std::vector<Command> spare;
        bool first = true;
        for (int c = 0; c < channels; ++c)
        {
            if (! isOn (pair, c + 1))
                continue;
            if (! first)
                spare.push_back ({ pair, c + 1, false });
            first = false;
        }
        applyAll (spare);
        commands.insert (commands.end(), spare.begin(), spare.end());
    }
    for (int pair = 1; pair <= pairs; ++pair)
    {
        const auto place = placeOf (pair);
        const auto wasOnAChannel = before[static_cast<size_t> (pair - 1)] && *before[static_cast<size_t> (pair - 1)] != returnIndex;
        if (! place && wasOnAChannel)
        {
            for (const auto& command : toAux (pair))
                commands.push_back (command);
        }
        else if (place && *place != returnIndex && isOn (pair, aux))
        {
            commands.push_back ({ pair, aux, false });
            apply (pair, aux, false);
        }
    }
    return commands;
}

std::vector<Command> Stems::toAux (int pair)
{
    apply (pair, aux, true);
    return { { pair, aux, true } };
}

void Stems::apply (int pair, int bus, bool on)
{
    auto& mask = masks[static_cast<size_t> (pair - 1)];
    if (on)
        mask |= bitOf (bus);
    else
        mask &= ~bitOf (bus);
}

void Stems::applyAll (const std::vector<Command>& commands)
{
    for (const auto& command : commands)
        apply (command.pair, command.bus, command.on);
}

juce::var Stems::asData() const
{
    juce::Array<juce::var> cursorList, sourceList;
    for (int cursor : cursors)
        cursorList.add (cursor);
    for (const auto& source : sources)
        sourceList.add (source ? juce::var (*source) : juce::var());
    auto* object = new juce::DynamicObject();
    object->setProperty ("cursors", juce::var (cursorList));
    object->setProperty ("sources", juce::var (sourceList));
    return juce::var (object);
}

Stems Stems::fromData (const juce::var& data)
{
    Stems s;
    if (data.getDynamicObject() == nullptr)
        return s;
    if (auto cursors = cursorsOf (data["cursors"]))
        for (int i = 0; i < channels; ++i)
        {
            const auto cursor = (*cursors)[static_cast<size_t> (i)];
            s.cursors[static_cast<size_t> (i)] = contains (ring (i), cursor) ? cursor : analogInput;
        }
    if (auto sources = sourcesOf (data["sources"]))
        s.sources = *sources;
    return s;
}
}
