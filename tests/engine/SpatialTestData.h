// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>

namespace a3test
{
inline const juce::File shippedSpatialDirectory { A3_SPATIAL_DATA };
inline const juce::File reaperBoothSpatialDirectory { A3_SPATIAL_REAPER_BOOTH_DATA };

// A scratch copy of a spatial directory the test may break.
struct ScratchDirectory
{
    explicit ScratchDirectory (const juce::File& source = shippedSpatialDirectory)
    {
        directory = juce::File::createTempFile ("a3-spatial");
        source.copyDirectoryTo (directory);
    }
    ~ScratchDirectory() { directory.deleteRecursively(); }
    juce::File root() const { return directory; }
    juce::File directory;
};
}
