// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeskFirst.h"

namespace a3core
{
namespace
{
// dict[address] = value: replaced in its place, or appended.
void put (DeskFirst::Pairs& pairs, std::unordered_map<std::string, size_t>& index, const DeskFirst::Pair& pair)
{
    const auto found = index.find (pair.first);
    if (found != index.end())
    {
        pairs[found->second].second = pair.second;
        return;
    }
    index[pair.first] = pairs.size();
    pairs.push_back (pair);
}
}

void DeskFirst::heard (const std::string& address, double value, bool fromDesk)
{
    const std::lock_guard<std::mutex> guard (lock);
    if (fromDesk || position.count (address))
        put (values, position, { address, value });
}

DeskFirst::Pairs DeskFirst::over (const Pairs& evening) const
{
    Pairs merged;
    std::unordered_map<std::string, size_t> index;
    for (const auto& pair : evening)
        put (merged, index, pair);

    const std::lock_guard<std::mutex> guard (lock);
    for (const auto& pair : values)
        put (merged, index, pair);
    return merged;
}
}
