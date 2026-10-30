// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace a3core
{
const char* libraryName();
// The program's version as its package carries it (03.0+283), handed in at build time; "dev" for a
// build no package made.
const char* programVersion();
}
