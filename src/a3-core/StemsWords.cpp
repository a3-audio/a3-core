// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StemsWords.h"

namespace a3core
{
std::pair<int, int> deckAndStem (int pair)
{
    return { (pair - 1) / Stems::stemsPerDeck + 1, (pair - 1) % Stems::stemsPerDeck + 1 };
}

int pairOf (int deck, int stem) { return (deck - 1) * Stems::stemsPerDeck + stem; }

std::vector<OscMessage> announcements (const Stems& stems, const Truth& truth)
{
    std::vector<OscMessage> out;
    for (int c = 0; c < Stems::channels; ++c)
    {
        const auto& source = stems.sources[static_cast<size_t> (c)];
        const auto plays = source == Stems::analogSource ? 0 : stems.channelMask (c);
        out.push_back ({ truth.address ("channel.stem", { { "ch", c + 1 } }), { pyInt (plays) } });
        out.push_back ({ truth.address ("channel.stem.cursor", { { "ch", c + 1 } }), { pyInt (stems.cursors[static_cast<size_t> (c)]) } });
    }
    std::vector<OscArg> onReturn { pyInt (stems.returnCursor) };
    for (int pair = 1; pair <= Stems::pairs; ++pair)
        onReturn.push_back (pyInt (stems.playsOnReturn (pair) ? 1 : 0));
    out.push_back ({ truth.address ("aux-return.stem"), onReturn });
    out.push_back ({ truth.address ("aux-return.stem.mode"), { pyInt (stems.returnMode) } });
    return out;
}

bool Settle::due (double now)
{
    if (! last || now - *last < delay)
        return false;
    last.reset();
    return true;
}

std::vector<Message> commandMessages (const std::vector<Command>& commands, const Truth& truth)
{
    std::vector<Message> out;
    for (const auto& command : commands)
    {
        const auto [deck, stem] = deckAndStem (command.pair);
        out.push_back ({ truth.address ("stemdeck.bus", { { "deck", deck }, { "stem", stem }, { "bus", command.bus } }),
                         pyInt (command.on ? 1 : 0) });
    }
    return out;
}

std::vector<Message> changedMessages (const std::vector<Message>& messages, std::map<std::string, OscArg>& lastSent)
{
    std::vector<Message> changed;
    for (const auto& message : messages)
    {
        const auto known = lastSent.find (message.first);
        if (known == lastSent.end() || ! sameAsPython (known->second, message.second))
            changed.push_back (message);
    }
    for (const auto& message : changed)
        lastSent.insert_or_assign (message.first, message.second);
    return changed;
}
}
