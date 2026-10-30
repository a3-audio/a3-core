// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The DEPARTMENTS page: the service file's subscribers read-only, the stored list editable, every entry
// checked as it is typed, nothing invalid sent to Core, and what Core says it sent each one.
#include <gtest/gtest.h>

#include "CoreWindowRig.h"
#include "DepartmentsPage.h"
#include "FakeView.h"
#include "Look.h"
#include "MainWindow.h"
#include "PageSnapshot.h"

namespace
{
using a3apptest::CoreWindowRig;
using a3apptest::expectPainted;
using a3apptest::snapshot;

const a3core::Department light { "light", "192.168.8.60", 7000, true };
const a3core::Department video { "video", "video-pc.local", 7100, true };

struct DepartmentsPageTest : ::testing::Test
{
    juce::MessageManagerLock lock;   // the test runner's dispatch thread must stay out of component code
    a3app::Look look;
    DepartmentsPageTest() { juce::LookAndFeel::setDefaultLookAndFeel (&look); }
    ~DepartmentsPageTest() override { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
};

// The page asks, the client fetches (the test drives it), the page shows.
void fetch (CoreWindowRig& rig, a3app::DepartmentsPage& page)
{
    page.refresh();
    rig.client->refreshNow (0);
    page.refresh();
}
}

TEST_F (DepartmentsPageTest, ShowsTheServiceFileAndTheStoredListWithWhatWasSent)
{
    CoreWindowRig rig;
    rig.serviceSubscribers = { { "stage", "192.168.8.80", 7200 } };
    rig.publishDepartments ({ light, video });
    a3app::DepartmentsPage page (*rig.client, 0);
    fetch (rig, page);
    EXPECT_EQ (page.serviceNames(), juce::StringArray ({ "stage" }));
    ASSERT_EQ (page.entryCount(), 2);
    EXPECT_EQ (page.entry (0), light);
    EXPECT_EQ (page.entry (1), video);
    EXPECT_NE (page.statusOf ("light").indexOf ("sending"), -1) << page.statusOf ("light");
    EXPECT_NE (page.serviceStatusOf ("stage").indexOf ("service file"), -1) << page.serviceStatusOf ("stage");
    EXPECT_FALSE (page.hasUnsaved());
    expectPainted (snapshot (page, "departments"));
}

TEST_F (DepartmentsPageTest, AnEntryThatIsOffSaysSo)
{
    CoreWindowRig rig;
    auto off = light;
    off.on = false;
    rig.publishDepartments ({ off });
    a3app::DepartmentsPage page (*rig.client, 0);
    fetch (rig, page);
    EXPECT_NE (page.statusOf ("light").indexOf ("off"), -1) << page.statusOf ("light");
}

TEST_F (DepartmentsPageTest, ANameInBothSaysTheServiceFileWins)
{
    CoreWindowRig rig;
    rig.serviceSubscribers = { { "light", "192.168.8.70", 7000 } };
    rig.publishDepartments ({ light });
    a3app::DepartmentsPage page (*rig.client, 0);
    fetch (rig, page);
    EXPECT_NE (page.statusOf ("light").indexOf ("service file"), -1) << page.statusOf ("light");
    EXPECT_NE (page.serviceStatusOf ("light").indexOf ("also in the list"), -1) << page.serviceStatusOf ("light");
    expectPainted (snapshot (page, "departments-name-in-both"));
}

TEST_F (DepartmentsPageTest, AGoodEntryIsSavedToCore)
{
    CoreWindowRig rig;
    a3app::DepartmentsPage page (*rig.client, 0);
    fetch (rig, page);
    page.addEntry();
    ASSERT_EQ (page.entryCount(), 1);
    page.setEntry (0, light);
    EXPECT_TRUE (page.problemOf (0).isEmpty()) << page.problemOf (0);
    EXPECT_TRUE (page.hasUnsaved());
    EXPECT_TRUE (page.canSave());
    EXPECT_TRUE (page.saveNow().wasOk()) << page.answerText();
    ASSERT_EQ (rig.departmentsSaved.size(), 1u);
    EXPECT_EQ (rig.departmentsSaved[0], (std::vector<a3core::Department> { light }));
    fetch (rig, page);
    EXPECT_FALSE (page.hasUnsaved());
    EXPECT_NE (page.statusOf ("light").indexOf ("sending"), -1) << page.statusOf ("light");
}

TEST_F (DepartmentsPageTest, AnInvalidEntryCannotBeSaved)
{
    CoreWindowRig rig;
    a3app::DepartmentsPage page (*rig.client, 0);
    fetch (rig, page);
    page.addEntry();
    for (const auto& bad : { a3core::Department { "", "192.168.8.60", 7000, true }, a3core::Department { "light", "192.168.8", 7000, true },
                             a3core::Department { "light", "192.168.8.60", 0, true }, a3core::Department { "light", "192.168.8.60", 70000, true },
                             a3core::Department { "mixer", "192.168.8.60", 7000, true } })
    {
        page.setEntry (0, bad);
        EXPECT_FALSE (page.problemOf (0).isEmpty()) << bad.name << " " << bad.host << " " << bad.port;
        EXPECT_FALSE (page.canSave());
        EXPECT_TRUE (page.saveNow().failed());
    }
    EXPECT_TRUE (rig.departmentsSaved.empty());
    expectPainted (snapshot (page, "departments-invalid"));
}

TEST_F (DepartmentsPageTest, APortThatIsNotANumberIsAProblem)
{
    CoreWindowRig rig;
    a3app::DepartmentsPage page (*rig.client, 0);
    page.addEntry();
    page.setEntryText (0, "light", "192.168.8.60", "70a");
    EXPECT_FALSE (page.problemOf (0).isEmpty());
    EXPECT_FALSE (page.canSave());
}

TEST_F (DepartmentsPageTest, TwoEntriesCannotShareAName)
{
    CoreWindowRig rig;
    a3app::DepartmentsPage page (*rig.client, 0);
    page.addEntry();
    page.addEntry();
    page.setEntry (0, light);
    auto again = video;
    again.name = "light";
    page.setEntry (1, again);
    EXPECT_FALSE (page.problemOf (1).isEmpty());
    EXPECT_FALSE (page.canSave());
}

TEST_F (DepartmentsPageTest, CoresRefusalIsShownAndNothingIsSaved)
{
    CoreWindowRig rig;
    a3app::DepartmentsPage page (*rig.client, 0);
    page.addEntry();
    auto lost = light;
    lost.host = "nowhere.invalid";
    page.setEntry (0, lost);
    EXPECT_TRUE (page.canSave());   // the form is right; whether the name resolves, Core decides
    EXPECT_TRUE (page.saveNow().failed());
    EXPECT_NE (page.answerText().indexOf ("does not resolve"), -1) << page.answerText();
    EXPECT_TRUE (rig.departmentsSaved.empty());
}

TEST_F (DepartmentsPageTest, RemovedEntriesAreSavedAway)
{
    CoreWindowRig rig;
    rig.publishDepartments ({ light, video });
    a3app::DepartmentsPage page (*rig.client, 0);
    fetch (rig, page);
    page.removeEntry (0);
    ASSERT_EQ (page.entryCount(), 1);
    EXPECT_TRUE (page.saveNow().wasOk()) << page.answerText();
    ASSERT_EQ (rig.departmentsSaved.size(), 1u);
    EXPECT_EQ (rig.departmentsSaved[0], (std::vector<a3core::Department> { video }));
}

TEST_F (DepartmentsPageTest, AnEditIsNotOverwrittenByCoresNextReport)
{
    CoreWindowRig rig;
    rig.publishDepartments ({ light });
    a3app::DepartmentsPage page (*rig.client, 0);
    fetch (rig, page);
    auto moved = light;
    moved.host = "192.168.8.61";
    page.setEntry (0, moved);
    rig.publishDepartments ({ light }, 4);
    fetch (rig, page);
    EXPECT_EQ (page.entry (0), moved);
    EXPECT_TRUE (page.hasUnsaved());
}

TEST_F (DepartmentsPageTest, WithoutCoreThePageSaysSo)
{
    a3app::CoreClient nobody (a3core::loopbackAddress(), 1, "/state/recall", false);
    a3app::DepartmentsPage page (nobody, 0);
    page.refresh();
    nobody.refreshNow (0);
    page.refresh();
    EXPECT_NE (page.answerText().indexOf ("no Core"), -1) << page.answerText();
    expectPainted (snapshot (page, "departments-none"));
}

TEST_F (DepartmentsPageTest, TheWindowHasThePageWhenItHasCore)
{
    CoreWindowRig rig;
    a3apptest::FakeView view;
    juce::AudioDeviceManager manager;
    manager.addAudioDeviceType (std::make_unique<a3apptest::NoDevices>());
    {
        a3app::MainWindow window (view, manager, {}, false, [] {}, { rig.client.get(), {} });
        EXPECT_TRUE (window.pageList().contains ("DEPARTMENTS"));   // not behind the monitor switch
        window.showPage ("DEPARTMENTS");
        EXPECT_EQ (window.currentPage(), "DEPARTMENTS");
        window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
        auto& content = *window.getContentComponent();
        expectPainted (a3apptest::snapshotAt (content, "portrait-departments", content.getWidth(), content.getHeight()));
    }
    a3app::MainWindow window (view, manager, {}, false, [] {}, {});
    EXPECT_FALSE (window.pageList().contains ("DEPARTMENTS"));
}
