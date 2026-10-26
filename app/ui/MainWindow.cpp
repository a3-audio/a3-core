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

class MainWindow::Tabs final : public juce::TabbedComponent
{
public:
    Tabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}
};

MainWindow::MainWindow (HostView& view, juce::AudioDeviceManager& manager, bool serviceMode, std::function<void()> quitApp)
    : juce::DocumentWindow ("a3-engine", palette::ground, juce::DocumentWindow::closeButton, false), quit (std::move (quitApp)), service (serviceMode)
{
    setLookAndFeel (&look);
    auto tabs = std::make_unique<Tabs>();
    tabs->setTabBarDepth (juce::jmax (32, windowHeight / 16));
    auto audioPage = std::make_unique<AudioPage> (view, manager);
    auto statusPage = std::make_unique<StatusPage> (view);
    audio = audioPage.get();
    status = statusPage.get();
    tabs->addTab ("AUDIO", palette::surface, audioPage.release(), true);
    tabs->addTab ("STATUS", palette::surface, statusPage.release(), true);
    tabs->setCurrentTabIndex (1);
    setContentOwned (tabs.release(), false);
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
    setUsingNativeTitleBar (true);   // puts the window on the desktop: needs a display
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
    audio->refresh();
    status->refresh();
}
}
