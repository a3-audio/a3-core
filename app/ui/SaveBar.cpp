// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SaveBar.h"

namespace a3app
{
juce::Result saveEdits (HostView& view)
{
    const auto config = view.config();
    const auto written = saveAppConfig (config, view.configFile());
    if (written.failed())
        return written;
    if (sameConfig (config, view.running()))
        return juce::Result::ok();
    return view.apply (config);
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
    auto area = getLocalBounds().reduced (getWidth() / 80, getHeight() / 8);
    saveButton.setBounds (area.removeFromRight (juce::jmax (80, getWidth() / 8)));
    pending.setBounds (area.removeFromLeft (area.getWidth() / 3));
    message.setBounds (area);
}
}
