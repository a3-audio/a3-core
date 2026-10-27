// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AudioPage.h"
#include "CalibrationPage.h"
#include "ChannelsPage.h"
#include "FilterPage.h"
#include "IoPage.h"
#include "SaveBar.h"
#include "StatusPage.h"
#include "ZonesPage.h"
#include <functional>

namespace a3app
{
// The window: the setup pages (AUDIO, CHANNELS, ZONES, I/O, CALIBRATION, FILTER) and STATUS, with the Save bar below. With service = true the close button only hides it
// (the app is a unit that only SIGTERM ends); otherwise it asks the app to quit.
class MainWindow final : public juce::DocumentWindow, private juce::Timer
{
public:
    // `spatialChoices` are the directories the ZONES page offers.
    MainWindow (HostView&, juce::AudioDeviceManager&, std::vector<juce::File> spatialChoices, bool service, std::function<void()> quit);
    ~MainWindow() override;
    // Puts the window on the desktop (needs a display); the constructor builds it without one,
    // so the pages and the close behaviour are testable headless.
    void open();
    void closeButtonPressed() override;

private:
    void timerCallback() override;

    class Tabs;
    Look look;
    std::function<void()> quit;
    const bool service;
    class Content;
    Content* content = nullptr;
};
}
