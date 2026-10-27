// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Ahead.h"

namespace a3core
{
namespace
{
bool isInt (const OscArg& value) { return value.tag == 'i' || value.tag == 'h' || value.tag == 'r'; }   // not a bool
bool isNumber (const OscArg& value) { return isInt (value) || value.tag == 'f' || value.tag == 'd'; }
}

std::optional<std::vector<OscArg>> aheadArguments (const std::vector<OscArg>& arguments)
{
    if (arguments.size() != 4)
        return std::nullopt;

    const auto& section = arguments[0];
    const auto& following = arguments[1];
    const auto& bars = arguments[2];
    const auto& energy = arguments[3];

    if (section.tag != 's' || following.tag != 's')
        return std::nullopt;
    if (! isInt (bars))
        return std::nullopt;
    if (! isNumber (energy))
        return std::nullopt;

    return std::vector<OscArg> { section, following, bars, pyFloat (*pythonFloat (energy)) };
}

bool fromTheFollowed (const std::string& senderHost, const std::optional<std::string>& followedHost)
{
    return followedHost.has_value() && senderHost == *followedHost;
}
}
