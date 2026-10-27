// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "SpatialConfig.h"

namespace a3bench
{
// rec_L, rec_R, then per zone (main, booth) <zone>_sub only if the layout has a
// sub, then <zone>_top_1..N. The counts come from the layouts, never from here.
std::vector<std::string> spatialPortNames (const a3::SpatialConfig&);

// --spatial DIR if given; else <home>/.config/a3/engine/spatial if it exists;
// else the package default. The package never writes ~/.config.
std::string chooseSpatialDirectory (const std::string& argument, const std::string& home,
                                    const std::function<bool (const std::string&)>& directoryExists);
}
