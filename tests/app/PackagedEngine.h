// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's package config as the rig has it after the install (/usr/share/a3-engine/engine.json with
// the default layout beside it), copied into a directory of its own.
#pragma once

#include <juce_core/juce_core.h>

#include "SpatialTestData.h"

namespace a3apptest
{
struct PackagedEngine
{
    PackagedEngine() : dir (juce::File::createTempFile ("a3-packaged-engine"))
    {
        dir.createDirectory();
        juce::File (A3_DEFAULT_ENGINE_CONFIG).copyFileTo (config);
        a3test::shippedSpatialDirectory.copyDirectoryTo (config.getSiblingFile ("spatial"));
    }
    ~PackagedEngine() { dir.deleteRecursively(); }
    PackagedEngine (const PackagedEngine&) = delete;
    PackagedEngine& operator= (const PackagedEngine&) = delete;

    juce::File dir;
    juce::File config = dir.getChildFile ("engine.json");
};
}
