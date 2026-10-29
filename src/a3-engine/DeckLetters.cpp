// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeckLetters.h"

namespace a3
{
std::string deckLetters (int slot)
{
    std::string letters;
    for (; slot > 0; slot = (slot - 1) / 26)
        letters.insert (letters.begin(), (char) ('A' + (slot - 1) % 26));
    return letters;
}

int deckSlotOf (const std::string& letters)
{
    if (letters.empty())
        return 0;
    int slot = 0;
    for (const char c : letters)
    {
        const char upper = c >= 'a' && c <= 'z' ? (char) (c - 'a' + 'A') : c;
        if (upper < 'A' || upper > 'Z')
            return 0;
        slot = slot * 26 + (upper - 'A' + 1);
    }
    return slot;
}

int deckSlotOfMeter (const std::string& name)
{
    static const std::string prefix = "deck_";
    return name.compare (0, prefix.size(), prefix) == 0 ? deckSlotOf (name.substr (prefix.size())) : 0;
}
}
