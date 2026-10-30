// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "DepartmentsPage.h"
#include "MonitorBenchPage.h"
#include "MonitorRegisterPage.h"
#include "MonitorTrafficPage.h"
#include "MonitorTruthPage.h"
#include "MonitorUnknownPage.h"
#include <limits>

namespace a3app
{
namespace
{
constexpr int refreshHz = 20;
constexpr int windowWidth = 1000, windowHeight = 680;
constexpr int pageGroup = 1;
constexpr int noteSeconds = 6;   // how long the keyboard's line stays before the footer is plain again
constexpr int minimumKey = 28;   // a fingertip-ish floor under the shares below
constexpr int keyRowShare = 18, footerShare = 16;   // a key row and the footer as shares of the window's height
const juce::String monitorSwitchName = "OSC MONITOR";
const juce::String tooltipSwitchName = "Tooltips";

bool readSwitch (const juce::File& settings, const juce::Identifier& key)
{
    if (! settings.existsAsFile())
        return false;
    const auto value = juce::JSON::parse (settings.loadFileAsString())[key];
    return value.isBool() && static_cast<bool> (value);
}

void writeSwitch (const juce::File& settings, const juce::Identifier& key, bool on)
{
    settings.getParentDirectory().createDirectory();
    auto* made = new juce::DynamicObject();
    const auto old = settings.existsAsFile() ? juce::JSON::parse (settings.loadFileAsString()) : juce::var();
    if (auto* properties = old.getDynamicObject())
        for (const auto& property : properties->getProperties())
            made->setProperty (property.name, property.value);
    made->setProperty (key, on);
    settings.replaceWithText (juce::JSON::toString (juce::var (made)) + "\n", false, false, "\n");
}
}

juce::File windowSettingsFile (const juce::File& configDir) { return configDir.getChildFile ("core/window.settings"); }
bool readMonitorSwitch (const juce::File& settings) { return readSwitch (settings, "monitor"); }
void writeMonitorSwitch (const juce::File& settings, bool shown) { writeSwitch (settings, "monitor", shown); }
bool readTooltipSwitch (const juce::File& settings) { return readSwitch (settings, "tooltips"); }
void writeTooltipSwitch (const juce::File& settings, bool shown) { writeSwitch (settings, "tooltips", shown); }

// The page keys in rows across the top (one radio group: the engine's row, then the monitor's), the shown
// page below them at the full width, the footer (KEYS, the switches, the Save bar) at the bottom. Pages are
// refreshed by the window's timer.
class MainWindow::Content final : public juce::Component
{
public:
    Content (HostView& view, juce::AudioDeviceManager& manager, std::vector<juce::File> spatialChoices, MonitorSetup monitorToUse,
             KeyboardSetup keyboardSetup, juce::File userSpatialRoot)
        : saveBar (view), monitor (std::move (monitorToUse))
    {
        const auto userRoot = userSpatialRoot;
        if (keyboardSetup.link != nullptr)
            addKeyboard (std::move (keyboardSetup));
        addPage ("AUDIO", std::make_unique<AudioPage> (view, manager));
        addPage ("CHANNELS", std::make_unique<ChannelsPage> (view));
        auto zones = std::make_unique<ZonesPage> (view, std::move (spatialChoices), userRoot);
        zones->onOpenSpeakers = [this] { show ("SPEAKERS"); };
        zonesPage = zones.get();
        renameKey.onClick = [this] { zonesPage->askRename(); };
        deleteKey.onClick = [this] { zonesPage->askDelete(); };
        addChildComponent (renameKey);
        addChildComponent (deleteKey);
        addPage ("ZONES", std::move (zones));
        addPage ("SPEAKERS", std::make_unique<SpeakersPage> (view, std::move (userSpatialRoot)));
        addPage ("I/O", std::make_unique<IoPage> (view));
        addPage ("CALIBRATION", std::make_unique<CalibrationPage> (view));
        addPage ("FILTER", std::make_unique<FilterPage> (view));
        addPage ("STATUS", std::make_unique<StatusPage> (view));
        if (monitor.core != nullptr)
        {
            auto& core = *monitor.core;
            // Core's, not the monitor's: no switch hides it. It asks Core only while it is shown.
            addPage ("DEPARTMENTS", std::make_unique<DepartmentsPage> (core), false, true);
            addPage ("TRAFFIC", std::make_unique<MonitorTrafficPage> (core), true);
            addPage ("TRUTH & DEVICES", std::make_unique<MonitorTruthPage> (core), true);
            addPage ("NOT UNDERSTOOD", std::make_unique<MonitorUnknownPage> (core), true);
            addPage ("REGISTER", std::make_unique<MonitorRegisterPage> (core), true);
            addPage ("BENCH", std::make_unique<MonitorBenchPage> (core), true);
            monitorSwitch.setToggleState (readMonitorSwitch (monitor.settings), juce::dontSendNotification);
            monitorSwitch.onClick = [this] { flip (monitorSwitch.getToggleState()); };
            addAndMakeVisible (monitorSwitch);
        }
        tooltipSwitch.onClick = [this] { showTooltips (tooltipSwitch.getToggleState()); };
        addAndMakeVisible (tooltipSwitch);
        applyTooltips (readTooltipSwitch (monitor.settings));
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
            showZonesKeys();
            return;
        }
    }

    bool monitorShown() const { return monitorSwitch.getToggleState(); }
    bool tooltipsShown() const { return tooltipSwitch.getToggleState(); }
    bool hasTooltipWindow() const { return tooltipWindow != nullptr; }
    WindowBands currentBands() const { return bands; }

    // A flip of the Tooltips switch: shown or not, and remembered (the one moment it is written).
    void showTooltips (bool wanted)
    {
        if (wanted == hasTooltipWindow())
            return tooltipSwitch.setToggleState (wanted, juce::dontSendNotification);
        applyTooltips (wanted);
        if (monitor.settings != juce::File())
            writeTooltipSwitch (monitor.settings, wanted);
    }

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
            if (! page.monitor && ! page.onlyWhenShown)
                page.refresh();   // the engine pages keep their pending edits current even when hidden
        refreshShown();
        showZonesKeys();
        saveBar.refresh();
        if (noteTicksLeft > 0 && --noteTicksLeft == 0)
            sayKeyboard ({});
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int key = juce::jmax (minimumKey, getHeight() / keyRowShare);
        bands.footer = area.removeFromBottom (juce::jmax (minimumKey, getHeight() / footerShare));
        layoutFooter (bands.footer);
        const bool monitorRow = monitorShown() && monitor.core != nullptr;
        bands.navigation = area.removeFromTop (key * (monitorRow ? 2 : 1));
        auto navigation = bands.navigation;
        layoutKeyRow (navigation.removeFromTop (key), false);
        if (monitorRow)
            layoutKeyRow (navigation, true);
        shareKeyText();
        if (keyboardNoteLabel.isVisible())
        {
            keyboardNoteLabel.setFont (juce::FontOptions ((float) key * 0.3f));
            keyboardNoteLabel.setBounds (area.removeFromBottom (key).reduced (getWidth() / 80, 0));
        }
        bands.page = area;
        for (auto& page : pages)
            page.component->setBounds (area);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (palette::ground);
        g.setColour (palette::raised);
        g.fillRect (bands.footer);
    }

private:
    struct Page
    {
        juce::String name;
        std::unique_ptr<juce::Component> component;
        std::function<void()> refresh;
        bool monitor = false;
        std::unique_ptr<juce::TextButton> key;
        bool onlyWhenShown = false;   // refreshed (and so asking Core) only while it is the page shown
    };

    // KEYS sits at the foot of the page list, where the hand already is. It never takes the focus: pressing it
    // must not leave the field it is for. Without onboard the window says so, once, where the keys are.
    void addKeyboard (KeyboardSetup setup)
    {
        keyboard = std::make_unique<OnScreenKeyboard> (std::move (setup.link), setup.timings);
        keys.setMouseClickGrabsKeyboardFocus (false);
        keys.setTooltip (restingTooltip());
        keys.onClick = [this] { keyboard->keysPressed(); };
        keyboard->onHint = [this] (const juce::String& line) { sayKeyboard (line); };
        look::warning (keyboardNoteLabel);
        keyboardNoteLabel.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (keys);
        addChildComponent (keyboardNoteLabel);
    }

    // What the tooltip says when no hint is up: the same whether onboard is there or not, as the hint was.
    juce::String restingTooltip() const
    {
        return keyboard->isAvailable() ? "On-screen keyboard (onboard): show or hide" : "On-screen keyboard: onboard is not installed";
    }

    void sayKeyboard (const juce::String& line)
    {
        keys.setTooltip (line.isNotEmpty() ? line : restingTooltip());
        keyboardNoteLabel.setText (line, juce::dontSendNotification);
        keyboardNoteLabel.setVisible (line.isNotEmpty());
        noteTicksLeft = line.isNotEmpty() ? noteSeconds * refreshHz : 0;
        resized();
    }

    template <class PageType>
    void addPage (const juce::String& name, std::unique_ptr<PageType> made, bool isMonitor = false, bool onlyWhenShown = false)
    {
        auto* raw = made.get();
        Page page { name, std::move (made), [raw] { raw->refresh(); }, isMonitor, std::make_unique<juce::TextButton> (name), onlyWhenShown };
        page.key->setRadioGroupId (pageGroup);
        page.key->setClickingTogglesState (false);   // show() decides which one is on
        page.key->onClick = [this, name] { show (name); };
        addChildComponent (*page.component);
        addAndMakeVisible (*page.key);
        pages.push_back (std::move (page));
    }

    // One row of page keys (the engine's or the monitor's), side by side in equal shares of the width.
    void layoutKeyRow (juce::Rectangle<int> row, bool monitorKeys)
    {
        std::vector<juce::TextButton*> keysInRow;
        for (auto& page : pages)
            if (page.monitor == monitorKeys && page.key->isVisible())
                keysInRow.push_back (page.key.get());
        if (keysInRow.empty())
            return;
        const int gap = juce::jmax (1, row.getHeight() / 16);
        const int width = row.getWidth() / (int) keysInRow.size();
        for (auto* key : keysInRow)
            key->setBounds (row.removeFromLeft (width).reduced (gap));
    }

    // Every page key's text at the size the longest name fits its key at: one size across the rows.
    void shareKeyText()
    {
        auto shared = std::numeric_limits<float>::max();
        for (auto& page : pages)
            if (page.key->isVisible())
                shared = juce::jmin (shared, fittedTextHeight (page.name, (float) page.key->getHeight() * keyTextShare,
                                                               (float) page.key->getWidth() * keyTextWidthShare));
        for (auto& page : pages)
            page.key->getProperties().set (sharedKeyTextHeight, (double) shared);
    }

    // KEYS, the monitor and Tooltips switches on the left; the Save bar (its Save on the right) takes the rest.
    void layoutFooter (juce::Rectangle<int> footer)
    {
        auto left = footer.reduced (getWidth() / 200, footer.getHeight() / 10);
        if (keyboard != nullptr)
            keys.setBounds (left.removeFromLeft (getWidth() / 8));
        if (monitor.core != nullptr)
            monitorSwitch.setBounds (left.removeFromLeft (getWidth() / 5));
        tooltipSwitch.setBounds (left.removeFromLeft (getWidth() / 7));
        if (renameKey.isVisible())
        {
            renameKey.setBounds (left.removeFromLeft (getWidth() / 8).reduced (left.getHeight() / 16, 0));
            deleteKey.setBounds (left.removeFromLeft (getWidth() / 8).reduced (left.getHeight() / 16, 0));
        }
        saveBar.setBounds (footer.withLeft (left.getX()));
    }

    // RENAME and DELETE belong to ZONES: shown on it only, each disabled with the reason as its tooltip.
    void showZonesKeys()
    {
        const bool onZones = ! pages.empty() && pages[(size_t) shown].component.get() == zonesPage;
        const auto deleteWhy = onZones ? zonesPage->deleteRefusal() : juce::String();
        const auto renameWhy = onZones ? zonesPage->renameRefusal() : juce::String();
        deleteKey.setEnabled (deleteWhy.isEmpty());
        deleteKey.setTooltip (deleteWhy.isEmpty() ? "Delete the variant picked in the list (asks first)" : "Cannot delete: " + deleteWhy);
        renameKey.setEnabled (renameWhy.isEmpty());
        renameKey.setTooltip (renameWhy.isEmpty() ? "Rename the variant picked in the list" : "Cannot rename: " + renameWhy);
        if (renameKey.isVisible() == onZones)
            return;
        renameKey.setVisible (onZones);
        deleteKey.setVisible (onZones);
        resized();
    }

    void applyTooltips (bool on)
    {
        tooltipSwitch.setToggleState (on, juce::dontSendNotification);
        tooltipWindow = on ? std::make_unique<juce::TooltipWindow> (this) : nullptr;   // without one JUCE shows no tooltip
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
        if (! pages.empty() && (pages[(size_t) shown].monitor || pages[(size_t) shown].onlyWhenShown))
            pages[(size_t) shown].refresh();
    }

    std::vector<Page> pages;
    int shown = 0;
    SaveBar saveBar;
    MonitorSetup monitor;
    juce::ToggleButton monitorSwitch { monitorSwitchName };
    juce::ToggleButton tooltipSwitch { tooltipSwitchName };
    ZonesPage* zonesPage = nullptr;
    juce::TextButton renameKey { "RENAME" }, deleteKey { "DELETE" };
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    WindowBands bands;
    juce::TextButton keys { "KEYS" };
    juce::Label keyboardNoteLabel;
    std::unique_ptr<OnScreenKeyboard> keyboard;
    int noteTicksLeft = 0;
    bool written = false;   // what the settings file says, as far as this window knows
};

MainWindow::MainWindow (HostView& view, juce::AudioDeviceManager& manager, std::vector<juce::File> spatialChoices,
                        bool serviceMode, std::function<void()> quitApp, MonitorSetup monitor, KeyboardSetup keyboard,
                        juce::File userSpatialRoot)
    : juce::DocumentWindow ("a3-core", palette::ground, juce::DocumentWindow::closeButton, false), quit (std::move (quitApp)), service (serviceMode)
{
    setLookAndFeel (&look);
    auto page = std::make_unique<Content> (view, manager, std::move (spatialChoices), std::move (monitor), std::move (keyboard),
                                           std::move (userSpatialRoot));
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
    refreshNow();
}

void MainWindow::refreshNow() { content->refresh(); }

juce::StringArray MainWindow::pageList() const { return content->pageList(); }
juce::String MainWindow::currentPage() const { return content->current(); }
void MainWindow::showPage (const juce::String& name) { content->show (name); }
void MainWindow::setMonitorShown (bool shown) { content->flip (shown); }
bool MainWindow::monitorShown() const { return content->monitorShown(); }
juce::TextButton* MainWindow::keysButton() { return content->keysButton(); }
juce::String MainWindow::keyboardNote() const { return content->keyboardNote(); }
void MainWindow::setTooltipsShown (bool shown) { content->showTooltips (shown); }
bool MainWindow::tooltipsShown() const { return content->tooltipsShown(); }
bool MainWindow::hasTooltipWindow() const { return content->hasTooltipWindow(); }
WindowBands MainWindow::bands() const { return content->currentBands(); }
}
