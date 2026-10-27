// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What Core alone knows about a channel and about the master: the fields the state file keeps and
// the recall answers with (a3-core.py's ChannelInfo and MasterInfo, without the track numbers --
// those described REAPER's tracks, and the engine has none).
#pragma once

#include <optional>

namespace a3core
{
struct ChannelInfo
{
    bool toggleFx = false, toggleCue = false;
    // Where the sound is and the 3D blend; nothing until Motion (or a file) said so. Never 0.0 for "unknown".
    std::optional<double> azimuth, elevation, threeD;
};

struct MasterInfo
{
    enum class FxMode { lowPass = 0, highPass = 1 };

    double phonesMix = 0;
    bool returnCue = false;
    FxMode fxMode = FxMode::lowPass;
};

using FxMode = MasterInfo::FxMode;
}
