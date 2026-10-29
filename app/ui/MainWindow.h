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
#include "SpeakersPage.h"
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

// ~/.config/a3/core/window.settings, {"monitor": true|false, "tooltips": true|false}. Read: a missing or broken
// file, or a missing key, is "off". A write of one switch keeps the other.
juce::File windowSettingsFile (const juce::File& configDir);
bool readMonitorSwitch (const juce::File& settings);
void writeMonitorSwitch (const juce::File& settings, bool shown);
bool readTooltipSwitch (const juce::File& settings);
void writeTooltipSwitch (const juce::File& settings, bool shown);

// The window's three bands, top to bottom, in its content's coordinates.
struct WindowBands
{
    juce::Rectangle<int> navigation, page, footer;
};

// The window: the setup pages' keys (AUDIO, CHANNELS, ZONES, SPEAKERS, I/O, CALIBRATION, FILTER, STATUS) in a row
// across the top; with a Core client and the OSC MONITOR switch on, the monitor's (TRAFFIC, TRUTH & DEVICES, NOT
// UNDERSTOOD, REGISTER, BENCH) in a second row. The shown page below them at the full width, and a footer: KEYS,
// the OSC MONITOR and Tooltips switches, and the Save bar. With service = true the close button only hides it
// (the app is a unit that only SIGTERM ends); otherwise it asks the app to quit.
class MainWindow final : public juce::DocumentWindow, private juce::Timer
{
public:
    // `spatialChoices` are the directories the ZONES page offers; `userSpatialRoot` is where SPEAKERS saves
    // user variants (<configDir>/engine/spatial; empty: it cannot save).
    MainWindow (HostView&, juce::AudioDeviceManager&, std::vector<juce::File> spatialChoices, bool service, std::function<void()> quit,
                MonitorSetup monitor = {}, KeyboardSetup keyboard = {}, juce::File userSpatialRoot = {});
    ~MainWindow() override;
    // Puts the window on the desktop (needs a display); the constructor builds it without one,
    // so the pages and the close behaviour are testable headless.
    void open();
    void closeButtonPressed() override;

    // The pages as their keys are shown (row by row), then the monitor switch (when there is one).
    juce::StringArray pageList() const;
    juce::String currentPage() const;
    void showPage (const juce::String& name);
    // The switch: shows or hides the monitor's pages, tells the client whether to read the traffic, and
    // writes the settings file (only here: a flip is the one write).
    void setMonitorShown (bool);
    bool monitorShown() const;
    // The Tooltips switch: the pages' help shows as tooltips only while it is on (a TooltipWindow exists only
    // then). A flip is remembered in the settings file, as the monitor switch is.
    void setTooltipsShown (bool);
    bool tooltipsShown() const;
    bool hasTooltipWindow() const;
    WindowBands bands() const;
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
