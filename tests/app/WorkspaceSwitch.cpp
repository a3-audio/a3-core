// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The window's way between the rig's apps (2026-10-30): the same switch as StemDeck, A3 Motion and the desk
// (a3-mixer 3b2ed83, DeskScreenTests.cpp), and i3's own full screen.
#include <gtest/gtest.h>
#include "FakePeer.h"
#include "WorkspaceSwitch.h"

namespace
{
using a3app::WorkspaceSwitch;
namespace screen = a3app::screen;
using Source = juce::MouseInputSource::InputSourceType;
using Argv = std::vector<std::string>;

const std::string rigWorkspaces =
    R"([{"name":"1:MOTION","focused":false},{"name":"2:STEMDECK","focused":false},{"name":"3:A3-CORE","focused":true}])";

// The switch on a window of its own, as on the rig, with a fake i3 that notes every command.
struct OnTheRig
{
    juce::MessageManagerLock lock;
    std::vector<Argv> calls;
    std::string reply = rigWorkspaces;
    a3apptest::PeerHost window;
    WorkspaceSwitch keys { [this] (const Argv& argv) {
        calls.push_back (argv);
        return argv.size() > 1 && argv[1] == "-t" ? reply : std::string();
    } };
    juce::ComponentPeer* peer = nullptr;

    OnTheRig()
    {
        window.setBounds (0, 0, 768, 989);
        keys.attachTo (window);
        keys.place ({ 0, 0, 768, 56 });
        window.addToDesktop (0);
        window.setVisible (true);
        peer = window.getPeer();
    }

    void down (Source s, juce::Point<float> at, juce::int64 t)
    {
        peer->handleMouseEvent (s, at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.f, 0.f, t);
    }
    void up (Source s, juce::Point<float> at, juce::int64 t) { peer->handleMouseEvent (s, at, juce::ModifierKeys(), 1.f, 0.f, t); }

    // One finger as the rig's X delivers it: touch#0, X's emulated mouse#0 2 ms later.
    void tap (juce::Point<float> at, juce::int64 t)
    {
        down (Source::touch, at, t);
        down (Source::mouse, at, t + 2);
        up (Source::touch, at, t + 60);
        up (Source::mouse, at, t + 62);
    }
    // A quick tap: the finger is up before X's copy goes down.
    void quickTap (juce::Point<float> at, juce::int64 t)
    {
        down (Source::touch, at, t);
        up (Source::touch, at, t + 1);
        down (Source::mouse, at, t + 2);
        up (Source::mouse, at, t + 3);
    }
    static juce::Point<float> centreOf (const juce::Component& c)
    {
        return c.getScreenBounds().getCentre().toFloat() - c.getTopLevelComponent()->getScreenPosition().toFloat();
    }
    std::vector<Argv> workspaceCommands() const
    {
        std::vector<Argv> out;
        for (const auto& c : calls)
            if (c.size() == 3 && c[1] == "-q")
                out.push_back (c);
        return out;
    }
};
}

// JUCE's setFullScreen is a maximise to i3, which tiles it under the bar: i3 is asked for its own full screen, on this window.
TEST (WorkspaceSwitch, I3IsAskedForFullScreenOnTheWindow)
{
    EXPECT_EQ (screen::fullScreenCommand (0x1a00007), (Argv { "i3-msg", "-q", "[id=27262983] fullscreen enable" }));
}

TEST (WorkspaceSwitch, TheWorkspacesAreI3s)
{
    const auto list = screen::parseWorkspaces (
        R"([{"name":"1:MOTION","focused":false},{"name":"3:A3-CORE","focused":true},{"name":"scratch","focused":false}])");
    ASSERT_EQ (list.size(), 2u);
    EXPECT_EQ (list[1].number, 3);
    EXPECT_EQ (list[1].label, "A3-CORE");
    EXPECT_TRUE (list[1].current);
    EXPECT_TRUE (screen::parseWorkspaces ("").empty());
    EXPECT_EQ (screen::goToCommand (2), (Argv { "i3-msg", "-q", "workspace number 2" }));
}

TEST (WorkspaceSwitch, StemDeckThenTheArrowAtTheRightEdge)
{
    OnTheRig rig;
    EXPECT_EQ (rig.keys.appKey().label(), "STEMDECK");
    EXPECT_EQ (rig.keys.arrowKey().label(), juce::String::fromUTF8 ("\xe2\x96\xbe"));
    const auto g = screen::switcherGeometry (768);
    EXPECT_EQ (rig.keys.arrowKey().getRight(), 768 - g.margin);
    EXPECT_EQ (rig.keys.appKey().getRight(), rig.keys.arrowKey().getX() - g.gap);
    EXPECT_TRUE (rig.keys.appKey().isEnabled());
}

// Its default choice is StemDeck: one tap, delivered twice by X, is one command.
TEST (WorkspaceSwitch, OneTapOnStemDeckGoesThereOnce)
{
    OnTheRig rig;
    rig.tap (OnTheRig::centreOf (rig.keys.appKey()), 1000);
    EXPECT_EQ (rig.workspaceCommands(), (std::vector<Argv> { screen::goToCommand (2) }));
    rig.calls.clear();
    rig.quickTap (OnTheRig::centreOf (rig.keys.appKey()), 2000);
    EXPECT_EQ (rig.workspaceCommands(), (std::vector<Argv> { screen::goToCommand (2) }));
}

// The arrow opens the list with one tap -- however X delivers it -- and the list stays open; a key in it goes to its
// workspace once and closes it; a tap beside it only closes it.
TEST (WorkspaceSwitch, OneTapOpensTheListAndOneChooses)
{
    for (const bool quick : { false, true })
    {
        OnTheRig rig;
        const auto arrow = OnTheRig::centreOf (rig.keys.arrowKey());
        quick ? rig.quickTap (arrow, 1000) : rig.tap (arrow, 1000);
        ASSERT_TRUE (rig.keys.panel().isVisible()) << quick;
        ASSERT_EQ (rig.keys.panel().keyCount(), 3) << quick;
        EXPECT_TRUE (rig.keys.panel().keyAt (2).current());   // 3:A3-CORE
        EXPECT_TRUE (rig.workspaceCommands().empty());

        quick ? rig.quickTap (OnTheRig::centreOf (rig.keys.panel().keyAt (0)), 2000)
              : rig.tap (OnTheRig::centreOf (rig.keys.panel().keyAt (0)), 2000);
        EXPECT_EQ (rig.workspaceCommands(), (std::vector<Argv> { screen::goToCommand (1) })) << quick;
        EXPECT_FALSE (rig.keys.panel().isVisible()) << quick;

        rig.tap (arrow, 3000);
        ASSERT_TRUE (rig.keys.panel().isVisible());
        rig.tap ({ 100.f, 600.f }, 4000);
        EXPECT_FALSE (rig.keys.panel().isVisible());
        EXPECT_EQ (rig.workspaceCommands().size(), 1u);
    }
}

TEST (WorkspaceSwitch, NoAnswerFromI3KeepsTheListClosed)
{
    OnTheRig rig;
    rig.reply.clear();
    rig.tap (OnTheRig::centreOf (rig.keys.arrowKey()), 1000);
    EXPECT_FALSE (rig.keys.panel().isVisible());
}
