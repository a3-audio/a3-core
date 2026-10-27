// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeckAudioReport.h"
#include <algorithm>

namespace a3app
{
std::vector<bool> deckAudioLinked (const IoMap& map, int decks, const std::vector<std::string>& missingPeers)
{
    std::vector<bool> linked;
    for (int slot = 1; slot <= decks; ++slot)
    {
        const auto prefix = "deck" + deckLetters (slot) + "_";   // the "_" keeps deckA apart from deckAA
        bool any = false, complete = true;
        for (const auto& [port, peers] : map.jack)
        {
            if (port.rfind (prefix, 0) != 0)
                continue;
            for (const auto& peer : peers)
            {
                any = true;
                complete = complete && std::find (missingPeers.begin(), missingPeers.end(), peer) == missingPeers.end();
            }
        }
        linked.push_back (any && complete);
    }
    return linked;
}

std::optional<std::vector<bool>> deckAudioReport (const BackendStatus& status, const IoMap& map, int decks)
{
    if (! status.connecting || ! status.linksPlanned)
        return std::nullopt;
    std::vector<std::string> missing;
    for (const auto& peer : status.missingPeers)
        missing.push_back (peer.toStdString());
    return deckAudioLinked (map, decks, missing);
}
}
