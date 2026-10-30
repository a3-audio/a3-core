// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Version.h"

#ifndef A3_CORE_VERSION
 #define A3_CORE_VERSION ""
#endif

namespace a3core
{
const char* libraryName() { return "a3-core-lib"; }

const char* programVersion()
{
    static constexpr const char* handedIn = A3_CORE_VERSION;
    return handedIn[0] != '\0' ? handedIn : "dev";
}
}
