// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialPorts.h"

namespace a3bench
{
namespace
{
const char* const packageSpatialDirectory = "/usr/share/a3-engine/spatial";

void appendZone (std::vector<std::string>& names, const std::string& zone, const a3::Layout& layout)
{
    if (layout.hasSub())
        names.push_back (zone + "_sub");
    for (size_t k = 1; k <= layout.topChannels.size(); ++k)
        names.push_back (zone + "_top_" + std::to_string (k));
}
}

std::vector<std::string> spatialPortNames (const a3::SpatialConfig& config)
{
    std::vector<std::string> names { "rec_L", "rec_R" };
    appendZone (names, "main", config.main.layout);
    appendZone (names, "booth", config.booth.layout);
    return names;
}

std::string chooseSpatialDirectory (const std::string& argument, const std::string& home,
                                    const std::function<bool (const std::string&)>& directoryExists)
{
    if (! argument.empty())
        return argument;
    if (! home.empty())
    {
        const auto user = home + "/.config/a3/engine/spatial";
        if (directoryExists (user))
            return user;
    }
    return packageSpatialDirectory;
}
}
