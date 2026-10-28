// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdexcept>
#include <juce_core/juce_core.h>
#include "OscVocabulary.h"

// The truth copy (tests/engine/data/a3-osc.json) as the app's tests read it.
namespace a3apptest
{
inline juce::String truthText() { return juce::File (A3_TRUTH_COPY).loadFileAsString(); }

// The truth copy with stemdeck.deck-hello's deck range ending at `last` instead of 4.
inline juce::String truthWithDeckRange (int last)
{
    const juce::String from = "\"pattern\": \"/stemdeck/{deck}/hello\", \"args\": \"s\", \"deck\": [1, 4]";
    const auto text = truthText();
    if (! text.contains (from))
        throw std::runtime_error ("the truth copy's stemdeck.deck-hello changed its form");
    return text.replace (from, from.dropLastCharacters (2) + juce::String (last) + "]");
}

inline a3app::EngineTruth engineTruth()
{
    juce::String error;
    auto truth = a3app::parseEngineTruth (truthText(), error);
    if (! truth)
        throw std::runtime_error (("the truth copy: " + error).toStdString());
    return *truth;
}

// The truth copy's deck range (stemdeck.deck-hello): the slots of a config that names no count.
inline int truthDecks() { return engineTruth().decks; }
}
