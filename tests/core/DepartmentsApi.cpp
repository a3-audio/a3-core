// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// GET and POST /api/departments over real HTTP on an ephemeral loopback port: the report the serve
// thread published, and a new list checked before it is handed to the service's save.
#include <stdexcept>

#include <gtest/gtest.h>

#include "CoreApi.h"
#include "HttpClient.h"
#include "HttpServer.h"
#include "Json.h"

using namespace a3core;
namespace hc = httpclient;

namespace
{
juce::var parsed (const std::string& body)
{
    juce::String error;
    auto value = parseJson (juce::String::fromUTF8 (body.data(), static_cast<int> (body.size())), error);
    EXPECT_EQ (error, juce::String()) << body;
    return value;
}

struct DepartmentsWindow
{
    CoreView view;
    std::vector<std::vector<Department>> saved;
    std::string refuseWith;   // what the save throws, when not empty
    bool withView = true, withSave = true;
    std::unique_ptr<HttpServer> server;

    bool open()
    {
        DepartmentsDoor door;
        door.resolves = [] (const std::string& host) { return host != "nowhere.invalid"; };
        if (withSave)
            door.save = [this] (const std::vector<Department>& list) {
                if (! refuseWith.empty())
                    throw DepartmentsRefused (refuseWith);
                saved.push_back (list);
            };
        Routes routes;
        addDepartmentRoutes (routes, withView ? &view : nullptr, std::move (door));
        server = std::make_unique<HttpServer> (loopbackAddress() + ":0", std::move (routes));
        return server->start ([] (const std::string&, bool) {});
    }

    ~DepartmentsWindow()
    {
        if (server)
            server->stop();
    }

    hc::Reply get() const { return hc::get (server->port(), "/api/departments"); }
    hc::Reply post (const std::string& body) const { return hc::post (server->port(), "/api/departments", body); }
};

std::string bodyOf (const std::vector<Department>& list)
{
    return canonicalBytes (objectOf ({ { "departments", departmentsJson (list) } }));
}

const Department light { "light", "192.168.8.60", 7000, true };
}

TEST (DepartmentsApi, GetAnswersTheReportCorePublished)
{
    DepartmentsWindow window;
    ASSERT_TRUE (window.open());
    window.view.publish ({ "{}", "{}", R"({"departments":[{"name":"light"}]})" });
    const auto reply = window.get();
    EXPECT_EQ (reply.status, 200);
    EXPECT_EQ (parsed (reply.body)["departments"][0]["name"].toString(), "light");
}

TEST (DepartmentsApi, WithoutAViewTheListIsEmpty)
{
    DepartmentsWindow window;
    window.withView = false;
    ASSERT_TRUE (window.open());
    const auto reply = window.get();
    EXPECT_EQ (reply.status, 200);
    ASSERT_TRUE (parsed (reply.body)["departments"].isArray());
    EXPECT_EQ (parsed (reply.body)["departments"].size(), 0);
}

TEST (DepartmentsApi, AGoodListIsSavedAndAnswered)
{
    DepartmentsWindow window;
    ASSERT_TRUE (window.open());
    const auto reply = window.post (bodyOf ({ light }));
    EXPECT_EQ (reply.status, 200) << reply.body;
    ASSERT_EQ (window.saved.size(), 1u);
    EXPECT_EQ (window.saved[0], (std::vector<Department> { light }));
    EXPECT_EQ (parsed (reply.body)["departments"][0]["host"].toString(), "192.168.8.60");
}

TEST (DepartmentsApi, AnEmptyListIsAList)
{
    DepartmentsWindow window;
    ASSERT_TRUE (window.open());
    EXPECT_EQ (window.post (bodyOf ({})).status, 200);
    ASSERT_EQ (window.saved.size(), 1u);
    EXPECT_TRUE (window.saved[0].empty());
}

TEST (DepartmentsApi, ABodyThatIsNotJsonSavesNothing)
{
    DepartmentsWindow window;
    ASSERT_TRUE (window.open());
    const auto reply = window.post ("not json");
    EXPECT_EQ (reply.status, 400);
    EXPECT_FALSE (parsed (reply.body)["problem"].toString().isEmpty());
    EXPECT_TRUE (window.saved.empty());
}

TEST (DepartmentsApi, ABadEntrySavesNothingAndSaysWhich)
{
    DepartmentsWindow window;
    ASSERT_TRUE (window.open());
    auto bad = light;
    bad.port = 0;
    const auto reply = window.post (bodyOf ({ bad }));
    EXPECT_EQ (reply.status, 400);
    EXPECT_NE (parsed (reply.body)["problem"].toString().indexOf ("light"), -1) << reply.body;
    EXPECT_TRUE (window.saved.empty());
}

TEST (DepartmentsApi, AHostThatDoesNotResolveSavesNothing)
{
    DepartmentsWindow window;
    ASSERT_TRUE (window.open());
    auto lost = light;
    lost.host = "nowhere.invalid";
    const auto reply = window.post (bodyOf ({ lost }));
    EXPECT_EQ (reply.status, 400);
    EXPECT_NE (parsed (reply.body)["problem"].toString().indexOf ("nowhere.invalid"), -1) << reply.body;
    EXPECT_TRUE (window.saved.empty());
}

TEST (DepartmentsApi, ASaveThatFailsIsSaid)
{
    DepartmentsWindow window;
    window.refuseWith = "cannot write the file";
    ASSERT_TRUE (window.open());
    const auto reply = window.post (bodyOf ({ light }));
    EXPECT_EQ (reply.status, 400);
    EXPECT_EQ (parsed (reply.body)["problem"].toString(), "cannot write the file");
}

TEST (DepartmentsApi, ACoreWithoutAFileRefusesTheSave)
{
    DepartmentsWindow window;
    window.withSave = false;
    ASSERT_TRUE (window.open());
    const auto reply = window.post (bodyOf ({ light }));
    EXPECT_EQ (reply.status, 400);
    EXPECT_FALSE (parsed (reply.body)["problem"].toString().isEmpty());
}
