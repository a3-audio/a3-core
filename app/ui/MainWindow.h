// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AudioPage.h"
#include "CoreClient.h"
#include "CalibrationPage.h"
#include "ChannelsPage.h"
#include "FilterPage.h"
#include "IoPage.h"
#include "OnScreenKeyboard.h"
#include "SaveBar.h"
#include "StatusPage.h"
#include "ZonesPage.h"
#include <functional>

namespace a3app
{
// The OSC monitor's pages beside the engine's: Core's client, and the file that remembers the switch (P10).
// No client: the window has no monitor and no switch.
struct MonitorSetup
{
    CoreClient* core = nullptr;
    juce::File settings;
};

// The on-screen keyboard (for the rig's touch screen): onboard behind a link. No link: no keyboard, no KEYS button.
struct KeyboardSetup
{
    std::shared_ptr<OnboardLink> link;
    KeyboardTimings timings;
};

// ~/.config/a3/core/window.settings, {"monitor": true|false}. Read: a missing or broken file is "off".
juce::File windowSettingsFile (const juce::File& configDir);
bool readMonitorSwitch (const juce::File& settings);
void writeMonitorSwitch (const juce::File& settings, bool shown);

// The window: the setup pages (AUDIO, CHANNELS, ZONES, I/O, CALIBRATION, FILTER) and STATUS in a page list on
// the left, the Save bar below; with a Core client also the OSC monitor's pages (TRAFFIC, TRUTH & DEVICES, NOT
// UNDERSTOOD, REGISTER, BENCH), shown by the switch at the bottom of the list. With service = true the close button only hides it
// (the app is a unit that only SIGTERM ends); otherwise it asks the app to quit.
class MainWindow final : public juce::DocumentWindow, private juce::Timer
{
public:
    // `spatialChoices` are the directories the ZONES page offers.
    MainWindow (HostView&, juce::AudioDeviceManager&, std::vector<juce::File> spatialChoices, bool service, std::function<void()> quit,
                MonitorSetup monitor = {}, KeyboardSetup keyboard = {});
    ~MainWindow() override;
    // Puts the window on the desktop (needs a display); the constructor builds it without one,
    // so the pages and the close behaviour are testable headless.
    void open();
    void closeButtonPressed() override;

    // The page list top to bottom as it is shown: the pages, then the monitor switch (when there is one).
    juce::StringArray pageList() const;
    juce::String currentPage() const;
    void showPage (const juce::String& name);
    // The switch: shows or hides the monitor's pages, tells the client whether to read the traffic, and
    // writes the settings file (only here: a flip is the one write).
    void setMonitorShown (bool);
    bool monitorShown() const;
    // The KEYS button (nullptr without a keyboard) and the one line the window says about the keyboard ("" when nothing is wrong).
    juce::TextButton* keysButton();
    juce::String keyboardNote() const;
    // What the timer does at each tick (the tests drive it without waiting).
    void refreshNow();

private:
    void timerCallback() override;

    Look look;
    std::function<void()> quit;
    const bool service;
    class Content;
    Content* content = nullptr;
};
}
