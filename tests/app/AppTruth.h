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

inline a3app::EngineTruth engineTruth()
{
    juce::String error;
    auto truth = a3app::parseEngineTruth (truthText(), error);
    if (! truth)
        throw std::runtime_error (("the truth copy: " + error).toStdString());
    return *truth;
}
}
