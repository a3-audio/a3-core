// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "DepartmentsPage.h"
#include "MonitorBenchPage.h"
#include "MonitorRegisterPage.h"
#include "MonitorTrafficPage.h"
#include "MonitorTruthPage.h"
#include "MonitorUnknownPage.h"
#include "SubPages.h"
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

// The page keys in rows across the top (one radio group: the engine's row, then the monitor's), the shown
// page below them at the full width, the footer (KEYS, the switches, the Save bar) at the bottom. Pages are
// refreshed by the window's timer.
class MainWindow::Content final : public juce::Component
{
public:
    Content (HostView& view, juce::AudioDeviceManager& manager, std::vector<juce::File> spatialChoices, MonitorSetup monitorToUse,
             KeyboardSetup keyboardSetup, juce::File userSpatialRoot, SystemSetup systemSetup)
        : saveBar (view), monitor (std::move (monitorToUse))
    {
        const auto userRoot = userSpatialRoot;
        if (keyboardSetup.link != nullptr)
            addKeyboard (std::move (keyboardSetup));
        // 2026-10-30: CHANNELS is a card on AUDIO, ZONES a sub-page of SPEAKERS, DEPARTMENTS one of I/O.
        addPage ("AUDIO", std::make_unique<AudioPage> (view, manager));
        auto speakersAndZones = std::make_unique<SubPages> ("SPEAKERS");
        speakers = speakersAndZones.get();
        auto speakerMap = std::make_unique<SpeakersPage> (view, std::move (userSpatialRoot));
        auto* speakerMapRaw = speakerMap.get();
        speakers->add ("SPEAKERS", std::move (speakerMap), [speakerMapRaw] { speakerMapRaw->refresh(); });
        auto zones = std::make_unique<ZonesPage> (view, std::move (spatialChoices), userRoot);
        zones->onOpenSpeakers = [this] { speakers->show ("SPEAKERS"); showZonesKeys(); };
        zonesPage = zones.get();
        speakers->add ("ZONES", std::move (zones), [this] { zonesPage->refresh(); });
        for (const auto& name : { "SPEAKERS", "ZONES" })
            speakers->key (name)->onClick = [this, name = juce::String (name)] { speakers->show (name); showZonesKeys(); };
        renameKey.onClick = [this] { zonesPage->askRename(); };
        deleteKey.onClick = [this] { zonesPage->askDelete(); };
        addChildComponent (renameKey);
        addChildComponent (deleteKey);
        addPage ("SPEAKERS", std::move (speakersAndZones));
        auto io = std::make_unique<IoPage> (view);
        ioPage = io.get();
        addPage ("I/O", std::move (io));
        addPage ("CALIBRATION", std::make_unique<CalibrationPage> (view));
        addPage ("FILTER", std::make_unique<FilterPage> (view));
        // 2026-10-30: STATUS is ENGINE (what the engine runs) and the Canvas's content, RIG and ABOUT; a seventh key in the
        // row made the key text smaller than a quarter of the key.
        if (systemSetup.probe == nullptr)
            idleProbe = std::make_unique<SystemProbe> (SystemSources {}, false);
        systemProbe = systemSetup.probe != nullptr ? systemSetup.probe : idleProbe.get();
        auto statusAndSystem = std::make_unique<SubPages> ("STATUS");
        status = statusAndSystem.get();
        auto statusPage = std::make_unique<StatusPage> (view);
        auto rigView = std::make_unique<SystemRigView> (*systemProbe, SystemConfirm {});
        auto aboutView = std::make_unique<SystemAboutView> (*systemProbe);
        auto* statusRaw = statusPage.get();
        auto* rigRaw = rigView.get();
        auto* aboutRaw = aboutView.get();
        status->add ("ENGINE", std::move (statusPage), [statusRaw] { statusRaw->refresh(); });
        status->add ("RIG", std::move (rigView), [rigRaw] { rigRaw->refresh(); });
        status->add ("ABOUT", std::move (aboutView), [aboutRaw] { aboutRaw->refresh(); });
        addPage ("STATUS", std::move (statusAndSystem));
        if (monitor.core != nullptr)
        {
            auto& core = *monitor.core;
            // Core's, not the monitor's: no switch hides it. It asks Core only while it is shown.
            departments = std::make_unique<DepartmentsPage> (core);
            ioPage->addDepartments (*departments, [this] { departments->refresh(); });
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

    // A page, or a sub-page by its key's name: ZONES (SPEAKERS'), INPUT, OUTPUT, DEPARTMENTS (I/O's).
    void show (const juce::String& name)
    {
        if (speakers != nullptr && speakers->has (name) && name != "SPEAKERS")
        {
            speakers->show (name);
            return show ("SPEAKERS");
        }
        if (status != nullptr && status->has (name))
        {
            status->show (name);
            return show ("STATUS");
        }
        if (ioPage != nullptr && (name == "INPUT" || name == "OUTPUT" || (name == "DEPARTMENTS" && departments != nullptr)))
        {
            ioPage->showSub (name == "INPUT" ? IoPage::Sub::input : name == "OUTPUT" ? IoPage::Sub::output : IoPage::Sub::departments);
            return show ("I/O");
        }
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
            askTheMachineWhileShown();
            refreshShown();
            showZonesKeys();
            return;
        }
    }

    bool monitorShown() const { return monitorSwitch.getToggleState(); }
    juce::String currentSub() const
    {
        const auto* page = pages[(size_t) shown].component.get();
        if (page == speakers)
            return speakers->shown();
        if (page == status)
            return status->shown();
        if (page == ioPage)
            return ioPage->shownSub() == IoPage::Sub::input ? "INPUT" : ioPage->shownSub() == IoPage::Sub::output ? "OUTPUT" : "DEPARTMENTS";
        return {};
    }
    WindowBands currentBands() const { return bands; }

    juce::TextButton* keysButton() { return keyboard != nullptr ? &keys : nullptr; }

    void setWorkspaceRunner (screen::I3Runner runner)
    {
        workspaces.reset();
        if (runner != nullptr)
        {
            workspaces = std::make_unique<WorkspaceSwitch> (std::move (runner));
            workspaces->attachTo (*this);
        }
        resized();
    }
    WorkspaceSwitch* workspaceSwitch() { return workspaces.get(); }
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
        askTheMachineWhileShown();   // a sub-page key does not pass show()
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
        auto firstRow = navigation.removeFromTop (key);
        if (workspaces != nullptr)
        {
            // StemDeck's and A3 Motion's sizes (switcherGeometry), at the row's right end; the page keys share the rest.
            const auto g = screen::switcherGeometry (getWidth());
            workspaces->place (firstRow.withTrimmedTop (juce::jmax (1, key / 16)).withTrimmedBottom (juce::jmax (1, key / 16)));
            firstRow.removeFromRight (g.margin + g.arrowWidth + g.gap + g.appKeyWidth + g.margin);
        }
        layoutKeyRow (firstRow, false);
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
        keys.onClick = [this] { keyboard->keysPressed(); };
        keyboard->onHint = [this] (const juce::String& line) { sayKeyboard (line); };
        look::warning (keyboardNoteLabel);
        keyboardNoteLabel.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (keys);
        addChildComponent (keyboardNoteLabel);
    }

    void sayKeyboard (const juce::String& line)
    {
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

    // KEYS and the monitor switch on the left; the Save bar (its Save on the right) takes the rest.
    void layoutFooter (juce::Rectangle<int> footer)
    {
        auto left = footer.reduced (getWidth() / 200, footer.getHeight() / 10);
        if (keyboard != nullptr)
            keys.setBounds (left.removeFromLeft (getWidth() / 8));
        if (monitor.core != nullptr)
            monitorSwitch.setBounds (left.removeFromLeft (getWidth() / 5));
        if (renameKey.isVisible())
        {
            renameKey.setBounds (left.removeFromLeft (getWidth() / 8).reduced (left.getHeight() / 16, 0));
            deleteKey.setBounds (left.removeFromLeft (getWidth() / 8).reduced (left.getHeight() / 16, 0));
        }
        saveBar.setBounds (footer.withLeft (left.getX()));
    }

    // RENAME and DELETE belong to ZONES: shown on it only. They are never disabled: a refused one says why on the
    // page when pressed (and the page's line under its list says it beforehand); a disabled key answered nothing.
    void showZonesKeys()
    {
        const bool onZones = ! pages.empty() && pages[(size_t) shown].component.get() == speakers && speakers->shown() == "ZONES";
        if (renameKey.isVisible() == onZones)
            return;
        renameKey.setVisible (onZones);
        deleteKey.setVisible (onZones);
        resized();
    }

    // systemd, git and Core's rig are asked only while RIG or ABOUT is shown.
    void askTheMachineWhileShown()
    {
        const bool shownNow = ! pages.empty() && pages[(size_t) shown].component.get() == status && status->shown() != "ENGINE";
        systemProbe->setWanted (shownNow);
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
    ZonesPage* zonesPage = nullptr;
    juce::TextButton renameKey { "RENAME" }, deleteKey { "DELETE" };
    SubPages* speakers = nullptr;
    IoPage* ioPage = nullptr;
    std::unique_ptr<DepartmentsPage> departments;
    std::unique_ptr<SystemProbe> idleProbe;
    SystemProbe* systemProbe = nullptr;
    SubPages* status = nullptr;
    WindowBands bands;
    juce::TextButton keys { "KEYS" };
    juce::Label keyboardNoteLabel;
    std::unique_ptr<OnScreenKeyboard> keyboard;
    std::unique_ptr<WorkspaceSwitch> workspaces;
    int noteTicksLeft = 0;
    bool written = false;   // what the settings file says, as far as this window knows
};

MainWindow::MainWindow (HostView& view, juce::AudioDeviceManager& manager, std::vector<juce::File> spatialChoices,
                        bool serviceMode, std::function<void()> quitApp, MonitorSetup monitor, KeyboardSetup keyboard,
                        juce::File userSpatialRoot, SystemSetup system)
    : juce::DocumentWindow ("a3-core", palette::ground, juce::DocumentWindow::closeButton, false), quit (std::move (quitApp)), service (serviceMode)
{
    setLookAndFeel (&look);
    auto page = std::make_unique<Content> (view, manager, std::move (spatialChoices), std::move (monitor), std::move (keyboard),
                                           std::move (userSpatialRoot), system);
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
juce::String MainWindow::currentSubPage() const { return content->currentSub(); }
WindowBands MainWindow::bands() const { return content->currentBands(); }
void MainWindow::setWorkspaceRunner (screen::I3Runner runner) { content->setWorkspaceRunner (std::move (runner)); }
WorkspaceSwitch* MainWindow::workspaceSwitch() { return content->workspaceSwitch(); }
}
