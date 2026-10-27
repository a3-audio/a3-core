// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "Version.h"

TEST(CoreSmoke, LibraryLinks)
{
    EXPECT_STREQ(a3core::libraryName(), "a3-core-lib");
}
