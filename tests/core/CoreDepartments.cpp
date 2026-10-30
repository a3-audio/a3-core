// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Departments in Core: the stored list subscribed at the start and changed live, the service file's
// --subscriber beside it (and winning a name in both), and the report the window shows per entry.
#include <gtest/gtest.h>

#include "CoreRig.h"

using namespace a3core;
using corerig::CoreRig;

namespace
{
Department light (const std::string& host = "192.168.8.60") { return { "light", host, 7000, true }; }

CoreRig::Options withDepartments (std::vector<Department> departments, std::vector<std::string> subscribers = {})
{
    CoreRig::Options options;
    options.departments = std::move (departments);
    options.subscribers = std::move (subscribers);
    return options;
}

// A value the desk moves, which Core passes on to every other subscriber.
void moveAFader (CoreRig& rig, double value = 0.5) { rig.osc (rig.desk, "/channel/1/volume", { pyFloat (value) }); }

bool contains (const std::vector<std::string>& lines, const std::string& line)
{
    return std::find (lines.begin(), lines.end(), line) != lines.end();
}

juce::var rowOf (const juce::var& report, const std::string& name, const std::string& from)
{
    const auto* rows = report["departments"].getArray();
    if (rows == nullptr)
        return {};
    for (const auto& row : *rows)
        if (row["name"].toString().toStdString() == name && row["from"].toString().toStdString() == from)
            return row;
    return {};
}
}

TEST (CoreDepartments, AStoredDepartmentIsToldWhatMotionIsTold)
{
    CoreRig rig (CoreRig::WithOptions {}, withDepartments ({ light() }));
    rig.clear();
    moveAFader (rig);
    EXPECT_TRUE (contains (rig.to ("light"), "/channel/1/volume f:0.5")) << ::testing::PrintToString (rig.to ("light"));
    EXPECT_EQ (rig.to ("light"), rig.to ("motion"));
}

TEST (CoreDepartments, ADepartmentThatIsOffIsToldNothing)
{
    auto off = light();
    off.on = false;
    CoreRig rig (CoreRig::WithOptions {}, withDepartments ({ off }));
    moveAFader (rig);
    EXPECT_TRUE (rig.to ("light").empty());
}

TEST (CoreDepartments, OneAddedLiveIsToldTheWholeStateAndThenEverything)
{
    CoreRig rig;
    moveAFader (rig, 0.25);
    rig.clear();
    rig.core->setDepartments ({ light() });
    EXPECT_TRUE (contains (rig.to ("light"), "/channel/1/volume f:0.25")) << ::testing::PrintToString (rig.to ("light"));
    const auto lines = rig.journal();
    EXPECT_TRUE (std::any_of (lines.begin(), lines.end(), [] (const std::string& line) { return line.rfind ("department light", 0) == 0; }))
        << ::testing::PrintToString (lines);
    rig.clear();
    moveAFader (rig, 0.75);
    EXPECT_TRUE (contains (rig.to ("light"), "/channel/1/volume f:0.75"));
}

TEST (CoreDepartments, OneRemovedLiveIsToldNothingMore)
{
    CoreRig rig (CoreRig::WithOptions {}, withDepartments ({ light() }));
    rig.core->setDepartments ({});
    rig.clear();
    moveAFader (rig);
    EXPECT_TRUE (rig.to ("light").empty());
    EXPECT_FALSE (rig.to ("motion").empty());
}

TEST (CoreDepartments, OneSwitchedOffLiveIsToldNothingMore)
{
    CoreRig rig (CoreRig::WithOptions {}, withDepartments ({ light() }));
    auto off = light();
    off.on = false;
    rig.core->setDepartments ({ off });
    rig.clear();
    moveAFader (rig);
    EXPECT_TRUE (rig.to ("light").empty());
}

TEST (CoreDepartments, OneMovedLiveIsToldAtItsNewPlace)
{
    CoreRig rig (CoreRig::WithOptions {}, withDepartments ({ light() }));
    rig.core->setDepartments ({ light ("192.168.8.61") });
    rig.clear();
    moveAFader (rig);
    const auto targets = rig.targetsOf ("light");
    ASSERT_FALSE (targets.empty());
    for (const auto& target : targets)
        EXPECT_EQ (target, "192.168.8.61:7000");
}

TEST (CoreDepartments, TheServiceFileWinsANameInBoth)
{
    CoreRig rig (CoreRig::WithOptions {}, withDepartments ({ light() }, { "light=192.168.8.70:7000" }));
    rig.clear();
    moveAFader (rig);
    const auto targets = rig.targetsOf ("light");
    ASSERT_EQ (targets.size(), 1u);   // once, not once per entry
    EXPECT_EQ (targets[0], "192.168.8.70:7000");

    const auto report = rig.core->departmentsJson();
    const auto service = rowOf (report, "light", "service");
    const auto window = rowOf (report, "light", "window");
    ASSERT_TRUE (service.isObject());
    ASSERT_TRUE (window.isObject());
    EXPECT_TRUE (static_cast<bool> (service["subscribed"]));
    EXPECT_FALSE (static_cast<bool> (window["subscribed"]));
    EXPECT_TRUE (static_cast<bool> (window["overridden"]));
}

TEST (CoreDepartments, AServiceFileSubscriberIsReportedReadOnly)
{
    CoreRig rig (CoreRig::WithOptions {}, withDepartments ({}, { "video=192.168.8.71:7100" }));
    const auto row = rowOf (rig.core->departmentsJson(), "video", "service");
    ASSERT_TRUE (row.isObject());
    EXPECT_EQ (row["host"].toString(), "192.168.8.71");
    EXPECT_EQ (static_cast<int> (row["port"]), 7100);
    EXPECT_TRUE (static_cast<bool> (row["on"]));
}

TEST (CoreDepartments, TheReportSaysWhatWasSentAndHowLongAgo)
{
    CoreRig rig (CoreRig::WithOptions {}, withDepartments ({ light() }));
    rig.at (10.0);
    moveAFader (rig);
    rig.at (12.5);
    const auto row = rowOf (rig.core->departmentsJson(), "light", "window");
    ASSERT_TRUE (row.isObject());
    EXPECT_GE (static_cast<int> (row["sent"]), 1);
    EXPECT_DOUBLE_EQ (static_cast<double> (row["last_sent"]), 2.5);
    EXPECT_TRUE (static_cast<bool> (row["subscribed"]));
}

TEST (CoreDepartments, OneNeverToldHasNoLastSend)
{
    auto off = light();
    off.on = false;
    CoreRig rig (CoreRig::WithOptions {}, withDepartments ({ off }));
    const auto row = rowOf (rig.core->departmentsJson(), "light", "window");
    ASSERT_TRUE (row.isObject());
    EXPECT_EQ (static_cast<int> (row["sent"]), 0);
    EXPECT_TRUE (row["last_sent"].isVoid());
    EXPECT_FALSE (static_cast<bool> (row["subscribed"]));
}

TEST (CoreDepartments, TheViewCarriesTheReport)
{
    CoreRig rig (CoreRig::WithOptions {}, withDepartments ({ light() }));
    rig.core->publishView();
    juce::String error;
    const auto published = parseJson (juce::String (rig.core->view().texts().departments), error);
    EXPECT_TRUE (rowOf (published, "light", "window").isObject()) << rig.core->view().texts().departments;
}
