// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The window role's process rules (P9): attach to what answers, start on a laptop what does not and is no
// unit's, ask once on quit, and never stop what it did not start. Core and the engine are the real routes on
// real HTTP servers; the platform is the fake, so a "start" is a child on paper.
#include <gtest/gtest.h>

#include "CoreWindowRig.h"
#include "FakePlatform.h"
#include "RemoteRig.h"
#include "Roles.h"
#include "ConfigFile.h"
#include "WindowProcess.h"

namespace
{
using a3app::Role;
using a3apptest::CoreWindowRig;
using a3apptest::RemoteRig;

// A loopback port nobody listens on: what a laptop without Core or engine looks like.
int closedPort()
{
    juce::StreamingSocket socket;
    socket.createListener (0, a3core::loopbackAddress());
    const int port = socket.getBoundPort();
    socket.close();
    return port;
}

struct NoCore
{
    a3app::CoreClient client { a3core::loopbackAddress(), closedPort(), "/state/recall", false };
};

struct NoEngine
{
    a3app::RemoteHostView remote { a3core::loopbackAddress(), closedPort(), false };
};

std::vector<std::string> commandOf (Role role) { return a3app::roleCommand ("/usr/bin/a3-core", role); }
}

TEST (Window, AttachesWhenCoreAnswers)
{
    CoreWindowRig core;
    RemoteRig engine;
    fake::FakePlatform platform;
    a3app::WindowProcess window (platform, *core.client, *engine.remote);

    EXPECT_TRUE (window.attach().empty());
    EXPECT_TRUE (window.coreAnswers());
    EXPECT_TRUE (window.engineAnswers());
    EXPECT_TRUE (platform.children.empty());
    EXPECT_FALSE (window.quitQuestion());   // nothing of its own: it quits without asking
}

TEST (Window, StartsWhatIsMissingOnALaptop)
{
    {
        NoCore core;
        NoEngine engine;
        fake::FakePlatform laptop;   // no unit enabled
        a3app::WindowProcess window (laptop, core.client, engine.remote);
        EXPECT_FALSE (window.coreAnswers());
        EXPECT_FALSE (window.engineAnswers());
        EXPECT_EQ (window.attach(), (std::vector<Role> { Role::core, Role::engine }));
        ASSERT_EQ (laptop.children.size(), 2u);
        EXPECT_EQ (laptop.spawned (commandOf (Role::core)).size(), 1u);
        EXPECT_EQ (laptop.spawned (commandOf (Role::engine)).size(), 1u);
        EXPECT_EQ (window.children(), (std::vector<Role> { Role::core, Role::engine }));
        EXPECT_TRUE (window.attach().empty());   // what it started is not started twice
        EXPECT_EQ (laptop.children.size(), 2u);
    }
    {
        CoreWindowRig answering;
        NoEngine engine;
        fake::FakePlatform laptop;
        a3app::WindowProcess window (laptop, *answering.client, engine.remote);
        EXPECT_EQ (window.attach(), (std::vector<Role> { Role::engine }));   // Core answers: only the engine
        EXPECT_TRUE (laptop.spawned (commandOf (Role::core)).empty());
    }
    {
        NoCore core;
        NoEngine engine;
        fake::FakePlatform rig;   // the rig with a3-core.service merely stopped
        rig.enabledUnits = { "a3-core.service" };
        a3app::WindowProcess window (rig, core.client, engine.remote);
        EXPECT_EQ (window.attach(), (std::vector<Role> { Role::engine }));
        EXPECT_TRUE (rig.spawned (commandOf (Role::core)).empty());   // the unit's, never a second copy
    }
    {
        NoCore core;
        NoEngine engine;
        fake::FakePlatform rig;   // the rig with a3-engine.service merely stopped
        rig.enabledUnits = { "a3-engine.service" };
        a3app::WindowProcess window (rig, core.client, engine.remote);
        EXPECT_EQ (window.attach(), (std::vector<Role> { Role::core }));
        EXPECT_TRUE (rig.spawned (commandOf (Role::engine)).empty());
    }
    {
        NoCore core;
        NoEngine engine;
        fake::FakePlatform laptop;
        laptop.failingSpawns = 2;   // nothing could be started
        a3app::WindowProcess window (laptop, core.client, engine.remote);
        EXPECT_TRUE (window.attach().empty());
        EXPECT_TRUE (window.children().empty());
    }
}

TEST (Window, ClosingLeavesWhatItDidNotStart)
{
    CoreWindowRig core;   // Core runs already (a unit, or someone else's)
    NoEngine engine;
    fake::FakePlatform laptop;
    a3app::WindowProcess window (laptop, *core.client, engine.remote);
    ASSERT_EQ (window.attach(), (std::vector<Role> { Role::engine }));
    const auto started = laptop.spawned (commandOf (Role::engine));
    ASSERT_EQ (started.size(), 1u);

    ASSERT_TRUE (window.quitQuestion());
    window.quit (true);
    EXPECT_EQ (started[0]->terminated, 1);    // its own child, stopped on yes
    EXPECT_TRUE (core.client->answers());     // Core, which it did not start, still runs
    EXPECT_EQ (laptop.children.size(), 1u);   // nothing else was started or touched
    // A unit is never stopped: the Platform seam has no call that could (unitEnabled and spawn only).

    // A plain close with a child of its own and the answer no: the child runs on.
    NoEngine engine2;
    fake::FakePlatform laptop2;
    a3app::WindowProcess window2 (laptop2, *core.client, engine2.remote);
    window2.attach();
    window2.quit (false);
    ASSERT_EQ (laptop2.children.size(), 1u);
    EXPECT_EQ (laptop2.children[0]->terminated, 0);
    EXPECT_EQ (laptop2.children[0]->killed, 0);
}

TEST (Window, AsksBeforeStoppingItsChildren)
{
    {
        NoCore core;
        NoEngine engine;
        fake::FakePlatform laptop;
        a3app::WindowProcess window (laptop, core.client, engine.remote);
        window.attach();
        const auto question = window.quitQuestion();
        ASSERT_TRUE (question);
        EXPECT_EQ (*question, "Stop Core and the engine too?");
        EXPECT_FALSE (window.quitQuestion());   // asked once
        window.quit (false);
        for (const auto& child : laptop.children)
        {
            EXPECT_EQ (child->terminated, 0);
            EXPECT_EQ (child->killed, 0);
        }
    }
    {
        NoCore core;
        NoEngine engine;
        fake::FakePlatform laptop;
        a3app::WindowProcess window (laptop, core.client, engine.remote);
        window.attach();
        laptop.children[0]->status = 1;   // Core ended by itself: only the engine is still its own
        EXPECT_EQ (window.children(), (std::vector<Role> { Role::engine }));
        EXPECT_EQ (window.quitQuestion(), std::optional<juce::String> ("Stop the engine too?"));
        laptop.children[1]->endsOnTerminate = false;   // ignores SIGTERM: killed after the wait
        window.quit (true);
        EXPECT_EQ (laptop.children[0]->terminated, 0);   // ended already: nothing sent to it
        EXPECT_EQ (laptop.children[1]->terminated, 1);
        EXPECT_EQ (laptop.children[1]->killed, 1);
    }
    EXPECT_EQ (a3app::stopQuestion ({ Role::core }), "Stop Core too?");
}

// The children it may leave running get a process group of their own: a Ctrl-C in the terminal the
// window was started from, or that terminal closing, does not reach them.
TEST (Window, ChildrenGetTheirOwnProcessGroup)
{
    NoCore core;
    NoEngine engine;
    fake::FakePlatform laptop;
    a3app::WindowProcess window (laptop, core.client, engine.remote);
    window.attach();
    ASSERT_EQ (laptop.children.size(), 2u);
    for (const auto& child : laptop.children)
        EXPECT_TRUE (child->ownGroup);
}

namespace
{
// The quit flow with a dialog on paper: the question it was shown and the answer the test gives.
struct PaperDialog
{
    int asked = 0;
    juce::String question;
    std::function<void (bool)> answer;
    a3app::QuitFlow::Ask ask()
    {
        return [this] (const juce::String& said, std::function<void (bool)> reply) {
            ++asked;
            question = said;
            answer = std::move (reply);
        };
    }
};
}

// A second close, or a stop signal, while the question is open is ignored: the window quits only on the
// answer. An answer that comes after the flow is gone does nothing.
TEST (Window, ASecondCloseWhileAskingIsIgnored)
{
    NoCore core;
    NoEngine engine;
    fake::FakePlatform laptop;
    a3app::WindowProcess window (laptop, core.client, engine.remote);
    window.attach();
    PaperDialog dialog;
    {
        a3app::QuitFlow flow (window, dialog.ask());
        flow.close();
        EXPECT_EQ (dialog.asked, 1);
        EXPECT_EQ (dialog.question, "Stop Core and the engine too?");
        flow.close();    // i3's kill again
        flow.signal();   // SIGTERM
        EXPECT_EQ (dialog.asked, 1);
        EXPECT_FALSE (flow.done());
        dialog.answer (true);
        EXPECT_TRUE (flow.done());
        for (const auto& child : laptop.children)
            EXPECT_EQ (child->terminated, 1);
    }
    {
        NoCore core2;
        NoEngine engine2;
        fake::FakePlatform laptop2;
        a3app::WindowProcess window2 (laptop2, core2.client, engine2.remote);
        window2.attach();
        PaperDialog late;
        {
            a3app::QuitFlow flow (window2, late.ask());
            flow.close();
        }
        late.answer (true);   // the flow is gone: nothing happens, nothing crashes
        for (const auto& child : laptop2.children)
            EXPECT_EQ (child->terminated, 0);
    }
    {
        NoCore core3;
        NoEngine engine3;
        fake::FakePlatform nothing;
        nothing.failingSpawns = 2;
        a3app::WindowProcess window3 (nothing, core3.client, engine3.remote);
        window3.attach();
        PaperDialog none;
        a3app::QuitFlow flow (window3, none.ask());
        flow.close();   // nothing of its own: quits without asking
        EXPECT_EQ (none.asked, 0);
        EXPECT_TRUE (flow.done());
        a3app::QuitFlow signalled (window3, none.ask());
        signalled.signal();
        EXPECT_TRUE (signalled.done());
    }
}

// The ZONES page offers the user's directory and the package's, from the config's own default.
TEST (Window, SpatialChoicesFollowTheConfigDefault)
{
    const juce::File configDir ("/home/tester/.config/a3");
    const auto installed = a3app::AppConfig{}.spatialDirectory;
    EXPECT_EQ (a3app::spatialCandidates (configDir),
               (std::vector<juce::File> { configDir.getChildFile ("engine/spatial"), installed,
                                          installed.getSiblingFile (installed.getFileName() + "-reaper-booth-6.1") }));
}
