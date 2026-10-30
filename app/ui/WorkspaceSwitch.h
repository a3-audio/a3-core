// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>


/** The window on the rig's screen, an i3 workspace among A3 Motion's, StemDeck's and the desk's: full screen, and the
    workspaces as their switch offers them (a3-mixer's DeskScreen.h at 3b2ed83, StemDeck's Workspaces.h, A3 Motion's
    io/Workspaces.hh). */
namespace a3app::screen
{
/** Runs a command line and returns what it printed, "" when it could not run. The tests hand in a fake,
    so no i3-msg runs in them. */
using I3Runner = std::function<std::string (const std::vector<std::string>& argv)>;

/** i3's own full screen for X window `window`: JUCE's setFullScreen is a maximise, which i3 tiles under its bar. */
std::vector<std::string> fullScreenCommand (unsigned long window);

struct Workspace
{
    int number = 0;
    std::string label;   // "3:REAPER" -> "REAPER"
    bool current = false;
};

/** What `i3-msg -t get_workspaces` printed: the workspaces with a number to go to; anything else is none. */
std::vector<Workspace> parseWorkspaces (const std::string& i3Reply);
std::vector<std::string> listCommand();
std::vector<std::string> goToCommand (int number);

/** StemDeck's workspace: where the switch's app key goes. */
inline constexpr int stemDeckWorkspace = 2;

/** Where the switch sits, StemDeck's and A3 Motion's numbers (shares of the window's width; at the rig's
    768 px their first sizes): the app key, then the arrow, at the window's right edge. */
struct SwitcherGeometry
{
    int margin = 0, arrowWidth = 0, gap = 0, appKeyWidth = 0, listTop = 0, listWidth = 0, listKeyHeight = 0,
        listGap = 0;
};
SwitcherGeometry switcherGeometry (int windowWidth);

/** Spawns the command (posix_spawnp, never fork: StemDeck's SpawnOutput) and returns its output; killed
    after a second. */
I3Runner systemI3Runner();
}




namespace a3app
{
/** One tap is one tap, however X delivers it. On the rig every finger arrives twice -- touch#0 and, about
    2 ms later, X's emulated mouse#0 -- and a quick tap can be up before its copy goes down. So a press is
    taken only if no press was taken in the last `twinMs`, and a gesture acts on the release of the source
    that was taken. Shared by every key of the switch and its list: the copy of the tap that opened the list
    must not close it again (the double taps StemDeck and A3 Motion need, 2026-10-30). */
class TapGate
{
public:
    static constexpr juce::int64 twinMs = 120;

    bool press (const juce::MouseEvent& e);
    /** True when this release ends the taken gesture. */
    bool release (const juce::MouseEvent& e);

private:
    std::optional<std::pair<juce::MouseInputSource::InputSourceType, int>> _source;
    std::optional<juce::int64> _lastPressMs;
};

/** A key in StemDeck's look (LookAndFeel_V4's button, StemDeck's Theme), as A3 Motion paints the same switch. */
class SwitchKey : public juce::Component
{
public:
    SwitchKey (TapGate& gate, juce::String label);
    const juce::String& label() const { return _label; }
    bool current() const { return _current; }
    void setCurrent (bool current);

    std::function<void()> onTap;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    TapGate& _gate;
    juce::String _label;
    bool _current = false;
};

/** The rig's workspaces as a column of keys inside the window (not a PopupMenu: a window of its own came
    up black on the rig, StemDeck 2026-09-30). Laid over the whole window: a tap beside the keys only closes it. */
class WorkspacePanel : public juce::Component
{
public:
    explicit WorkspacePanel (TapGate& gate) : _gate (gate) {}
    void show (const std::vector<screen::Workspace>& entries);
    int keyCount() const { return _keys.size(); }
    SwitchKey& keyAt (int i) { return *_keys[i]; }

    std::function<void (int number)> onChosen;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    TapGate& _gate;
    juce::OwnedArray<SwitchKey> _keys;
    juce::Rectangle<int> _column;
};

/** StemDeck's and A3 Motion's way between the rig's apps: STEMDECK (its default choice, workspace 2) and the
    arrow that lists every workspace, at the window's right edge where they have it. */
class WorkspaceSwitch
{
public:
    explicit WorkspaceSwitch (screen::I3Runner runner);

    /** Adds the keys, and the list laid over the whole of `parent`, to it. */
    void attachTo (juce::Component& parent);
    /** Puts the keys at the window's right edge in the height of `bar`. */
    void place (juce::Rectangle<int> bar);

    SwitchKey& appKey() { return _app; }
    SwitchKey& arrowKey() { return _arrow; }
    WorkspacePanel& panel() { return _panel; }

private:
    void showList();

    screen::I3Runner _i3;
    juce::Component* _window = nullptr;
    TapGate _gate;
    SwitchKey _app { _gate, "STEMDECK" };
    SwitchKey _arrow { _gate, juce::String::fromUTF8 ("\xe2\x96\xbe") };
    WorkspacePanel _panel { _gate };
};
}
