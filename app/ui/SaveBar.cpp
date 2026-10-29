// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SaveBar.h"

namespace a3app
{
namespace
{
bool putBack (const juce::File& file, const juce::String& text)
{
    juce::TemporaryFile temporary (file);
    return temporary.getFile().replaceWithText (text) && temporary.overwriteTargetFileWithTemporary();
}
}

juce::Result saveEdits (HostView& view)
{
    const auto config = view.config();
    const auto file = view.configFile();
    const bool existed = file.existsAsFile();
    const auto previous = existed ? file.loadFileAsString() : juce::String();
    const auto written = saveAppConfig (config, file);
    if (written.failed())
        return written;
    if (sameConfig (config, view.running()))
        return juce::Result::ok();
    const auto applied = view.apply (config);
    if (applied.wasOk())
        return applied;

    // A file the engine could not start from must not stay: the next boot would fail the same way.
    const bool restored = existed ? putBack (file, previous) : ! file.existsAsFile() || file.deleteFile();
    return juce::Result::fail ("not applied: " + applied.getErrorMessage()
                               + (restored ? ", previous config kept" : ", and the previous config could not be put back"));
}

SaveBar::SaveBar (HostView& v) : view (v)
{
    look::warning (pending);
    look::failure (message);
    saveButton.onClick = [this] { save(); };
    for (auto* child : std::initializer_list<juce::Component*> { &pending, &message, &saveButton })
        addAndMakeVisible (child);
}

SaveBar::~SaveBar() = default;

void SaveBar::save()
{
    const auto result = saveEdits (view);
    if (result.failed())
        look::say (message, result.getErrorMessage());
    else
        look::say (message, {});
    refresh();
}

void SaveBar::refresh()
{
    look::say (pending, sameConfig (view.config(), view.running()) ? juce::String() : "not saved - applies after Save");
}

void SaveBar::paint (juce::Graphics& g) { g.fillAll (palette::raised); }

void SaveBar::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 80, getHeight() / 10);
    saveButton.setBounds (area.removeFromRight (juce::jmax (area.getHeight() * 2, getWidth() / 5)));
    // Two lines beside Save: in the window's footer the bar shares its row with KEYS and the switches.
    pending.setBounds (area.removeFromTop (area.getHeight() / 2));
    message.setBounds (area);
}
}
