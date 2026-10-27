// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The truth after O13: a3-core's feat/core-truth 78230b9 (tests/engine/data/a3-osc.json), where the
// engine, not the analyzer, sends the meters. B0Truth.h stays the truth of the ported B1 suites and
// of every recorded scenario; the suites that pin the engine's meters (VuForward, the hand-written
// scenario) load this one.
#pragma once

#include "Json.h"
#include "Truth.h"

namespace enginetruth
{
inline const a3core::Truth& truth()
{
    static const a3core::Truth loaded = [] {
        juce::String error;
        const auto file = juce::File (A3_CORE_TEST_DATA).getChildFile ("../../engine/data/a3-osc.json");
        return a3core::Truth (a3core::parseJson (file.loadFileAsString(), error));
    }();
    return loaded;
}
}
