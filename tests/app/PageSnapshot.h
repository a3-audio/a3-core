// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// A page painted to an image: the snapshot of one visual state. A page that cannot paint (a crash on first
// paint, an empty component) fails here, not on the rig. A3_SNAPSHOT_DIR keeps the pictures.
#pragma once
#include <cstdlib>
#include <set>
#include <gtest/gtest.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace a3apptest
{
// An audio device type with no devices: the page's selector must not probe ALSA in a test.
class NoDevices final : public juce::AudioIODeviceType
{
public:
    NoDevices() : juce::AudioIODeviceType ("none") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override { return {}; }
    int getDefaultDeviceIndex (bool) const override { return -1; }
    int getIndexOfDevice (juce::AudioIODevice*, bool) const override { return -1; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override { return nullptr; }
};

// The rig's screen: the window in portrait on a 768 x 1024 panel.
constexpr int portraitWidth = 768, portraitHeight = 1024;

// `page` at `width` x `height`, painted (A3_SNAPSHOT_DIR keeps it as <name>.png).
inline juce::Image snapshotAt (juce::Component& page, const juce::String& name, int width, int height)
{
    page.setSize (width, height);
    auto image = page.createComponentSnapshot (page.getLocalBounds(), false, 1.0f);
    if (const char* dir = std::getenv ("A3_SNAPSHOT_DIR"))
    {
        juce::File out (juce::String (dir) + "/" + name + ".png");
        juce::FileOutputStream stream (out);
        if (stream.openedOk()) { stream.setPosition (0); stream.truncate(); juce::PNGImageFormat().writeImageToStream (image, stream); }
    }
    return image;
}

inline juce::Image snapshot (juce::Component& page, const juce::String& name) { return snapshotAt (page, name, 900, 640); }

inline int distinctColours (const juce::Image& image)
{
    std::set<juce::uint32> seen;
    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            seen.insert (image.getPixelAt (x, y).getARGB());
    return (int) seen.size();
}

inline void expectPainted (const juce::Image& image)
{
    ASSERT_TRUE (image.isValid());
    EXPECT_GT (image.getWidth(), 0);
    EXPECT_GT (distinctColours (image), 4);   // text and faces over the ground, not a flat fill
}
}
