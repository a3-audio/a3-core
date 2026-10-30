// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// A department (a light or video desk that is told every A3 message) as the window keeps it: what an
// entry may be, the list as JSON and as Core's file, and which entries Core subscribes.
#include <gtest/gtest.h>

#include "Departments.h"

using namespace a3core;

namespace
{
const HostResolves everyHost = [] (const std::string&) { return true; };
const HostResolves noHost = [] (const std::string&) { return false; };

Department light() { return { "light", "192.168.8.60", 7000, true }; }
Department video() { return { "video", "video-pc.local", 7100, true }; }

std::vector<std::string> namesOf (const std::vector<Client>& clients)
{
    std::vector<std::string> names;
    for (const auto& client : clients)
        names.push_back (client.name);
    return names;
}

struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a3-departments", "", false);
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};
}

// One entry.

TEST (Departments, ANameIsNeeded)
{
    EXPECT_TRUE (nameProblem ("").has_value());
    EXPECT_TRUE (nameProblem ("   ").has_value());
    EXPECT_FALSE (nameProblem ("light").has_value());
}

TEST (Departments, ANameCannotLookLikeADeskOrACommandLine)
{
    EXPECT_TRUE (nameProblem ("desk@host").has_value());   // a followed desk's name has an @
    EXPECT_TRUE (nameProblem ("light=1").has_value());     // --subscriber NAME=HOST:PORT could not say it
    EXPECT_TRUE (nameProblem ("two words").has_value());
}

TEST (Departments, CoresOwnClientsCannotBeNamed)
{
    for (const auto* taken : { "mixer", "motion", "engine", "self" })
        EXPECT_TRUE (nameProblem (taken).has_value()) << taken;
}

TEST (Departments, AHostIsAnIpv4AddressOrAHostName)
{
    EXPECT_FALSE (hostProblem ("192.168.8.60").has_value());
    EXPECT_FALSE (hostProblem ("video-pc.local").has_value());
    EXPECT_FALSE (hostProblem ("localhost").has_value());
    EXPECT_TRUE (hostProblem ("").has_value());
    EXPECT_TRUE (hostProblem ("192.168.8").has_value());
    EXPECT_TRUE (hostProblem ("192.168.8.256").has_value());
    EXPECT_TRUE (hostProblem ("video pc").has_value());
    EXPECT_TRUE (hostProblem ("-video").has_value());
    EXPECT_TRUE (hostProblem ("video..pc").has_value());
    EXPECT_TRUE (hostProblem ("192.168.8.60:7000").has_value());
}

TEST (Departments, APortIsOneTo65535)
{
    EXPECT_TRUE (portProblem (0).has_value());
    EXPECT_FALSE (portProblem (1).has_value());
    EXPECT_FALSE (portProblem (65535).has_value());
    EXPECT_TRUE (portProblem (65536).has_value());
    EXPECT_TRUE (portProblem (-1).has_value());
}

// The list.

TEST (Departments, AGoodListHasNoProblem)
{
    EXPECT_EQ (listProblem ({ light(), video() }, everyHost), std::nullopt);
    EXPECT_EQ (listProblem ({}, everyHost), std::nullopt);
}

TEST (Departments, TwoEntriesCannotShareAName)
{
    auto again = video();
    again.name = "light";
    const auto problem = listProblem ({ light(), again }, everyHost);
    ASSERT_TRUE (problem.has_value());
    EXPECT_NE (problem->find ("light"), std::string::npos) << *problem;
}

TEST (Departments, AHostThatDoesNotResolveIsRefusedAndNamed)
{
    const auto problem = listProblem ({ video() }, noHost);
    ASSERT_TRUE (problem.has_value());
    EXPECT_NE (problem->find ("video-pc.local"), std::string::npos) << *problem;
}

TEST (Departments, AnEntryThatIsOffIsCheckedToo)
{
    auto off = light();
    off.on = false;
    off.port = 0;
    EXPECT_TRUE (listProblem ({ off }, everyHost).has_value());
}

// JSON and the file.

TEST (Departments, TheListSurvivesJson)
{
    auto off = video();
    off.on = false;
    const std::vector<Department> list { light(), off };
    const auto back = departmentsFrom (departmentsJson (list));
    ASSERT_TRUE (std::holds_alternative<std::vector<Department>> (back)) << std::get<std::string> (back);
    EXPECT_EQ (std::get<std::vector<Department>> (back), list);
}

TEST (Departments, JsonOfTheWrongShapeIsRefusedWithAReason)
{
    juce::String error;
    for (const auto* text : { R"({"name": "light"})", R"([{"name": "light", "host": "192.168.8.60", "port": "7000", "on": true}])",
                              R"([{"name": "light", "host": "192.168.8.60", "port": 7000}])", R"([{"host": "192.168.8.60", "port": 7000, "on": true}])" })
    {
        const auto back = departmentsFrom (juce::JSON::parse (text));
        ASSERT_TRUE (std::holds_alternative<std::string> (back)) << text;
        EXPECT_FALSE (std::get<std::string> (back).empty()) << text;
    }
}

TEST (Departments, TheFileLivesInCoresConfig)
{
    EXPECT_EQ (departmentsFile (juce::File ("/home/aaa/.config/a3")).getFullPathName(), "/home/aaa/.config/a3/core/departments.json");
}

TEST (Departments, ASavedListIsReadBack)
{
    TempDir temp;
    const auto file = departmentsFile (temp.dir);
    ASSERT_TRUE (saveDepartments (file, { light(), video() }));
    const auto stored = loadDepartments (file);
    EXPECT_TRUE (stored.problems.empty());
    EXPECT_EQ (stored.list, (std::vector<Department> { light(), video() }));
}

TEST (Departments, NoFileIsNoDepartmentAndNoProblem)
{
    TempDir temp;
    const auto stored = loadDepartments (departmentsFile (temp.dir));
    EXPECT_TRUE (stored.list.empty());
    EXPECT_TRUE (stored.problems.empty());
}

TEST (Departments, ABrokenFileIsSaidAndStopsNothing)
{
    TempDir temp;
    const auto file = departmentsFile (temp.dir);
    file.getParentDirectory().createDirectory();
    file.replaceWithText ("{ not json");
    const auto stored = loadDepartments (file);
    EXPECT_TRUE (stored.list.empty());
    ASSERT_EQ (stored.problems.size(), 1u);
}

TEST (Departments, AHandEditedBadEntryIsLeftOutAndTheOthersKept)
{
    TempDir temp;
    const auto file = departmentsFile (temp.dir);
    file.getParentDirectory().createDirectory();
    file.replaceWithText (R"({"departments": [
        {"name": "light", "host": "192.168.8.60", "port": 7000, "on": true},
        {"name": "video", "host": "192.168.8.61", "port": 70000, "on": true},
        {"name": "light", "host": "192.168.8.62", "port": 7001, "on": true},
        {"name": "mixer", "host": "192.168.8.63", "port": 7002, "on": true}]})");
    const auto stored = loadDepartments (file);
    EXPECT_EQ (stored.list, (std::vector<Department> { light() }));
    EXPECT_EQ (stored.problems.size(), 3u);
}

// Who is subscribed.

TEST (Departments, AnEntryThatIsOnIsSubscribed)
{
    auto off = video();
    off.on = false;
    const auto clients = departmentClients ({ light(), off }, {});
    ASSERT_EQ (namesOf (clients), (std::vector<std::string> { "light" }));
    EXPECT_EQ (clients[0].to, (Endpoint { "192.168.8.60", 7000 }));
}

TEST (Departments, TheServiceFileWinsANameInBoth)
{
    EXPECT_EQ (namesOf (departmentClients ({ light(), video() }, { "light" })), (std::vector<std::string> { "video" }));
}
