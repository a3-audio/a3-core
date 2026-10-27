// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "MonitorBenchPage.h"
#include "MonitorRegisterPage.h"
#include "MonitorTrafficPage.h"
#include "MonitorTruthPage.h"
#include "MonitorUnknownPage.h"

namespace a3app
{
namespace
{
constexpr int refreshHz = 20;
constexpr int windowWidth = 1000, windowHeight = 680;
constexpr int pageGroup = 1;
constexpr int noteSeconds = 6;   // how long the keyboard's line stays before the list is plain again
const juce::String monitorSwitchName = "OSC MONITOR";
}

juce::File windowSettingsFile (const juce::File& configDir) { return configDir.getChildFile ("core/window.settings"); }

bool readMonitorSwitch (const juce::File& settings)
{
    if (! settings.existsAsFile())
        return false;
    const auto parsed = juce::JSON::parse (settings.loadFileAsString());
    return parsed["monitor"].isBool() && static_cast<bool> (parsed["monitor"]);
}

void writeMonitorSwitch (const juce::File& settings, bool shown)
{
    settings.getParentDirectory().createDirectory();
    auto* made = new juce::DynamicObject();
    made->setProperty ("monitor", shown);
    settings.replaceWithText (juce::JSON::toString (juce::var (made)) + "\n", false, false, "\n");
}

// The page list on the left (one radio group of keys, the monitor switch at its bottom), the shown page
// beside it and the Save bar below. Pages are refreshed by the window's timer.
class MainWindow::Content final : public juce::Component
{
public:
    Content (HostView& view, juce::AudioDeviceManager& manager, std::vector<juce::File> spatialChoices, MonitorSetup monitorToUse, KeyboardSetup keyboardSetup)
        : saveBar (view), monitor (std::move (monitorToUse))
    {
        if (keyboardSetup.link != nullptr)
            addKeyboard (std::move (keyboardSetup));
        addPage ("AUDIO", std::make_unique<AudioPage> (view, manager));
        addPage ("CHANNELS", std::make_unique<ChannelsPage> (view));
        addPage ("ZONES", std::make_unique<ZonesPage> (view, std::move (spatialChoices)));
        addPage ("I/O", std::make_unique<IoPage> (view));
        addPage ("CALIBRATION", std::make_unique<CalibrationPage> (view));
        addPage ("FILTER", std::make_unique<FilterPage> (view));
        addPage ("STATUS", std::make_unique<StatusPage> (view));
        if (monitor.core != nullptr)
        {
            auto& core = *monitor.core;
            addPage ("TRAFFIC", std::make_unique<MonitorTrafficPage> (core), true);
            addPage ("TRUTH & DEVICES", std::make_unique<MonitorTruthPage> (core), true);
            addPage ("NOT UNDERSTOOD", std::make_unique<MonitorUnknownPage> (core), true);
            addPage ("REGISTER", std::make_unique<MonitorRegisterPage> (core), true);
            addPage ("BENCH", std::make_unique<MonitorBenchPage> (core), true);
            monitorSwitch.setToggleState (readMonitorSwitch (monitor.settings), juce::dontSendNotification);
            monitorSwitch.onClick = [this] { flip (monitorSwitch.getToggleState()); };
            addAndMakeVisible (monitorSwitch);
        }
        addAndMakeVisible (saveBar);
        applySwitch();
        show ("STATUS");
    }

    juce::StringArray pageList() const
    {
        juce::StringArray names;
        for (const auto& page : pages)
            if (page.key->isVisible())
                names.add (page.name);
        if (monitor.core != nullptr)
            names.add (monitorSwitchName);
        return names;
    }

    juce::String current() const { return pages[(size_t) shown].name; }

    void show (const juce::String& name)
    {
        for (size_t i = 0; i < pages.size(); ++i)
        {
            const bool picked = pages[i].name == name && pages[i].key->isVisible();
            if (! picked)
                continue;
            shown = (int) i;
            for (size_t j = 0; j < pages.size(); ++j)
            {
                pages[j].component->setVisible (j == i);
                pages[j].key->setToggleState (j == i, juce::dontSendNotification);
            }
            refreshShown();
            return;
        }
    }

    bool monitorShown() const { return monitorSwitch.getToggleState(); }

    juce::TextButton* keysButton() { return keyboard != nullptr ? &keys : nullptr; }
    juce::String keyboardNote() const { return keyboardNoteLabel.getText(); }

    // A flip of the switch: the one moment the settings file is written (P10).
    void flip (bool wanted)
    {
        monitorSwitch.setToggleState (wanted, juce::dontSendNotification);
        if (monitor.core == nullptr || wanted == written)
            return applySwitch();
        writeMonitorSwitch (monitor.settings, wanted);
        applySwitch();
    }

    void refresh()
    {
        for (auto& page : pages)
            if (! page.monitor)
                page.refresh();   // the engine pages keep their pending edits current even when hidden
        refreshShown();
        saveBar.refresh();
        if (noteTicksLeft > 0 && --noteTicksLeft == 0)
            sayKeyboard ({});
    }

    void resized() override
    {
        auto area = getLocalBounds();
        saveBar.setBounds (area.removeFromBottom (juce::jmax (36, getHeight() / 14)));
        auto list = area.removeFromLeft (juce::jmax (120, getWidth() / 6)).reduced (getWidth() / 200);
        const int key = juce::jmax (28, list.getHeight() / 14);
        if (keyboard != nullptr)
        {
            keys.setBounds (list.removeFromBottom (key).reduced (0, key / 16));
            if (keyboardNoteLabel.getText().isNotEmpty())
            {
                keyboardNoteLabel.setFont (juce::FontOptions ((float) key * 0.3f));
                keyboardNoteLabel.setBounds (list.removeFromBottom (key * 3 / 2));
            }
        }
        if (monitor.core != nullptr)
            monitorSwitch.setBounds (list.removeFromBottom (key));
        for (auto& page : pages)
        {
            if (! page.key->isVisible())
                continue;
            if (&page == firstMonitorPage())
                list.removeFromTop (key / 3);   // a gap between the engine's pages and the monitor's
            page.key->setBounds (list.removeFromTop (key).reduced (0, key / 16));
        }
        for (auto& page : pages)
            page.component->setBounds (area);
    }

    void paint (juce::Graphics& g) override { g.fillAll (palette::ground); }

private:
    struct Page
    {
        juce::String name;
        std::unique_ptr<juce::Component> component;
        std::function<void()> refresh;
        bool monitor = false;
        std::unique_ptr<juce::TextButton> key;
    };

    // KEYS sits at the foot of the page list, where the hand already is. It never takes the focus: pressing it
    // must not leave the field it is for. Without onboard the window says so, once, where the keys are.
    void addKeyboard (KeyboardSetup setup)
    {
        keyboard = std::make_unique<OnScreenKeyboard> (std::move (setup.link), setup.timings);
        keys.setMouseClickGrabsKeyboardFocus (false);
        keys.setTooltip (keyboard->isAvailable() ? "On-screen keyboard (onboard): show or hide" : "On-screen keyboard: onboard is not installed");
        keys.onClick = [this] { keyboard->keysPressed(); };
        keyboard->onHint = [this] (const juce::String& line) { sayKeyboard (line); };
        look::warning (keyboardNoteLabel);
        keyboardNoteLabel.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (keys);
        addChildComponent (keyboardNoteLabel);
    }

    void sayKeyboard (const juce::String& line)
    {
        keys.setTooltip (line.isNotEmpty() ? line : "On-screen keyboard (onboard): show or hide");
        keyboardNoteLabel.setText (line, juce::dontSendNotification);
        keyboardNoteLabel.setVisible (line.isNotEmpty());
        noteTicksLeft = line.isNotEmpty() ? noteSeconds * refreshHz : 0;
        resized();
    }

    template <class PageType>
    void addPage (const juce::String& name, std::unique_ptr<PageType> made, bool isMonitor = false)
    {
        auto* raw = made.get();
        Page page { name, std::move (made), [raw] { raw->refresh(); }, isMonitor, std::make_unique<juce::TextButton> (name) };
        page.key->setRadioGroupId (pageGroup);
        page.key->setClickingTogglesState (false);   // show() decides which one is on
        page.key->onClick = [this, name] { show (name); };
        addChildComponent (*page.component);
        addAndMakeVisible (*page.key);
        pages.push_back (std::move (page));
    }

    const Page* firstMonitorPage() const
    {
        for (const auto& page : pages)
            if (page.monitor)
                return &page;
        return nullptr;
    }

    void applySwitch()
    {
        const bool on = monitor.core != nullptr && monitorSwitch.getToggleState();
        written = on;
        if (monitor.core != nullptr)
            monitor.core->setMonitorShown (on);
        for (auto& page : pages)
            if (page.monitor)
                page.key->setVisible (on);
        if (! pages.empty() && ! pages[(size_t) shown].key->isVisible())
            show ("STATUS");   // a hidden page is not left showing
        resized();
    }

    void refreshShown()
    {
        if (! pages.empty() && pages[(size_t) shown].monitor)
            pages[(size_t) shown].refresh();
    }

    std::vector<Page> pages;
    int shown = 0;
    SaveBar saveBar;
    MonitorSetup monitor;
    juce::ToggleButton monitorSwitch { monitorSwitchName };
    juce::TextButton keys { "KEYS" };
    juce::Label keyboardNoteLabel;
    std::unique_ptr<OnScreenKeyboard> keyboard;
    int noteTicksLeft = 0;
    bool written = false;   // what the settings file says, as far as this window knows
};

MainWindow::MainWindow (HostView& view, juce::AudioDeviceManager& manager, std::vector<juce::File> spatialChoices,
                        bool serviceMode, std::function<void()> quitApp, MonitorSetup monitor, KeyboardSetup keyboard)
    : juce::DocumentWindow ("a3-core", palette::ground, juce::DocumentWindow::closeButton, false), quit (std::move (quitApp)), service (serviceMode)
{
    setLookAndFeel (&look);
    auto page = std::make_unique<Content> (view, manager, std::move (spatialChoices), std::move (monitor), std::move (keyboard));
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

juce::StringArray MainWindow::pageList() const { return content->pageList(); }
juce::String MainWindow::currentPage() const { return content->current(); }
void MainWindow::showPage (const juce::String& name) { content->show (name); }
void MainWindow::setMonitorShown (bool shown) { content->flip (shown); }
bool MainWindow::monitorShown() const { return content->monitorShown(); }
juce::TextButton* MainWindow::keysButton() { return content->keysButton(); }
juce::String MainWindow::keyboardNote() const { return content->keyboardNote(); }
}
