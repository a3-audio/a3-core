// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

namespace a3app
{
namespace
{
constexpr int refreshHz = 20;
constexpr int windowWidth = 900, windowHeight = 640;
}

// The tabs over the Save bar. Pages are refreshed by the window's timer.
class MainWindow::Content final : public juce::Component
{
public:
    Content (HostView& view, juce::AudioDeviceManager& manager, std::vector<juce::File> spatialChoices)
        : tabs (juce::TabbedButtonBar::TabsAtTop), saveBar (view),
          audio (new AudioPage (view, manager)), channels (new ChannelsPage (view)),
          zones (new ZonesPage (view, std::move (spatialChoices))), io (new IoPage (view)),
          calibration (new CalibrationPage (view)), filter (new FilterPage (view)), status (new StatusPage (view))
    {
        tabs.setTabBarDepth (juce::jmax (32, windowHeight / 16));
        tabs.addTab ("AUDIO", palette::surface, audio, true);
        tabs.addTab ("CHANNELS", palette::surface, channels, true);
        tabs.addTab ("ZONES", palette::surface, zones, true);
        tabs.addTab ("I/O", palette::surface, io, true);
        tabs.addTab ("CALIBRATION", palette::surface, calibration, true);
        tabs.addTab ("FILTER", palette::surface, filter, true);
        tabs.addTab ("STATUS", palette::surface, status, true);
        tabs.setCurrentTabIndex (tabs.getNumTabs() - 1);
        addAndMakeVisible (tabs);
        addAndMakeVisible (saveBar);
    }

    void refresh()
    {
        audio->refresh();
        channels->refresh();
        zones->refresh();
        io->refresh();
        calibration->refresh();
        filter->refresh();
        status->refresh();
        saveBar.refresh();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        saveBar.setBounds (area.removeFromBottom (juce::jmax (36, getHeight() / 14)));
        tabs.setBounds (area);
    }

private:
    juce::TabbedComponent tabs;
    SaveBar saveBar;
    AudioPage* audio;   // owned by the tabs
    ChannelsPage* channels;
    ZonesPage* zones;
    IoPage* io;
    CalibrationPage* calibration;
    FilterPage* filter;
    StatusPage* status;
};

MainWindow::MainWindow (HostView& view, juce::AudioDeviceManager& manager, std::vector<juce::File> spatialChoices,
                        bool serviceMode, std::function<void()> quitApp)
    : juce::DocumentWindow ("a3-engine", palette::ground, juce::DocumentWindow::closeButton, false), quit (std::move (quitApp)), service (serviceMode)
{
    setLookAndFeel (&look);
    auto page = std::make_unique<Content> (view, manager, std::move (spatialChoices));
    content = page.get();
    setContentOwned (page.release(), false);
    setResizable (true, false);
    setSize (windowWidth, windowHeight);
    startTimerHz (refreshHz);
}

MainWindow::~MainWindow()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MainWindow::open()
{
    centreWithSize (getWidth(), getHeight());   // asks the desktop for its displays
    setUsingNativeTitleBar (true);
    addToDesktop();   // the constructor leaves the window off the desktop; this creates the peer and needs a display
    setVisible (true);
}

void MainWindow::closeButtonPressed()
{
    if (service)
        setVisible (false);   // the unit keeps running; only SIGTERM ends it
    else
        quit();
}

void MainWindow::timerCallback()
{
    if (! isVisible())
        return;
    content->refresh();
}
}
