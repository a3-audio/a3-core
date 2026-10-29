// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

namespace a3
{
// A deck slot's letters, with no cap: 1 = "A", 26 = "Z", 27 = "AA", 28 = "AB" (ports deck{X}_*, R12).
std::string deckLetters (int slotOneBased);
// The slot those letters name, either case ("C", "c", "AA"); 0 for anything else.
int deckSlotOf (const std::string& letters);
// The slot a /vu meter name "deck_<letters>" meters ("deck_c" is 3); 0 for any other name.
int deckSlotOfMeter (const std::string& meterName);
}
