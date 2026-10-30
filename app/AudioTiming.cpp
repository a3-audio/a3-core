// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioTiming.h"

namespace a3app
{
const std::vector<int>& pipewireBlocks()
{
    static const std::vector<int> blocks { 32, 64, 128, 256, 512, 1024 };
    return blocks;
}

AudioTiming timingOf (const BackendSettings& backend) { return { backend.blockSize }; }

Command forceQuantumCommand (int block)
{
    const int forced = block == defaultBlock ? 0 : block;
    return { "pw-metadata", "-n", "settings", "0", "clock.force-quantum", std::to_string (forced) };
}

juce::File retiredPeriodsDropIn (const juce::File& home)
{
    return home.getChildFile (".config/wireplumber/wireplumber.conf.d/80-a3-engine-alsa.conf");
}

bool removeRetiredPeriodsDropIn (const juce::File& home)
{
    const auto file = retiredPeriodsDropIn (home);
    return file.existsAsFile() && file.deleteFile();
}

juce::String timingText (int block) { return "Block " + juce::String (block) + " frames"; }
}
