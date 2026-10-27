// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// `a3-core --render-truth` against B1's a3-osc-render (a3-core 7a7b2441f74e) as tools/parity/
// record_render_cli.py recorded it in temporary homes -- stdout, stderr, exit code and the files left
// behind -- and `a3-core --truth host|port`, which replaces the viewer recipe's Python.
#include <gtest/gtest.h>

#include <map>
#include <regex>

#include "FakePlatform.h"
#include "Json.h"
#include "Subcommands.h"

namespace
{
const juce::File data (A3_RENDER_CLI_DATA);
const char* const placeholder = "@HOME@";

std::string read (const juce::File& file)
{
    juce::MemoryBlock block;
    EXPECT_TRUE (file.loadFileAsData (block)) << file.getFullPathName();
    return std::string (static_cast<const char*> (block.getData()), block.getSize());
}

void write (const juce::File& file, const std::string& bytes)
{
    ASSERT_TRUE (file.getParentDirectory().createDirectory());
    ASSERT_TRUE (file.replaceWithData (bytes.data(), bytes.size()));
}

// Where the parser stopped is worded by the parser: Python's "line 1 column 1 (char 0)", ours "at byte 0"
// (Json.cpp, shared with every reader of the truth). The reason before it is compared.
std::string withoutParserPosition (const std::string& text)
{
    static const std::regex position (R"(( at byte \d+)|(: line \d+ column \d+ \(char \d+\)))");
    return std::regex_replace (text, position, "");
}

std::string replaceAll (std::string text, const std::string& from, const std::string& to)
{
    for (size_t at = text.find (from); at != std::string::npos; at = text.find (from, at + to.size()))
        text.replace (at, from.size(), to);
    return text;
}

// path (relative to `root`) -> bytes, for every file below it
std::map<std::string, std::string> filesBelow (const juce::File& root)
{
    std::map<std::string, std::string> found;
    for (const auto& file : root.findChildFiles (juce::File::findFiles, true))
        found[file.getRelativePathFrom (root).toStdString()] = read (file);
    return found;
}

struct Case
{
    std::string name;
    juce::StringArray argv;
    std::map<std::string, std::string> env;
    int exitCode;
};

std::vector<Case> casesNamed (const std::string& prefix)
{
    juce::String error;
    const auto index = a3core::parseJson (data.getChildFile ("cases.json").loadFileAsString(), error);
    EXPECT_EQ (error, juce::String());
    std::vector<Case> found;
    for (const auto& entry : *index.getArray())
    {
        Case one;
        one.name = entry["name"].toString().toStdString();
        if (one.name.rfind (prefix, 0) != 0)
            continue;
        for (const auto& word : *entry["argv"].getArray())
            one.argv.add (word.toString());
        if (auto* env = entry["env"].getDynamicObject())
            for (const auto& property : env->getProperties())
                one.env[property.name.toString().toStdString()] = property.value.toString().toStdString();
        one.exitCode = static_cast<int> (entry["exit"]);
        found.push_back (one);
    }
    return found;
}

// A run of one recorded case in a home of its own.
struct RecordedRun
{
    RecordedRun (const Case& one)
    {
        home = juce::File::createTempFile ("a3-render-cli");
        home.createDirectory();
        platform.homeDir = home;
        platform.host = "a3nuc1";
        platform.env["A3_OSC_TRUTH"] = data.getChildFile ("a3-osc.json").getFullPathName().toStdString();
        for (const auto& [name, value] : one.env)
            platform.env[name] = replaceAll (value, placeholder, home.getFullPathName().toStdString());
        write (platform.packageShare().getChildFile ("pipewire/pipewire-aes67.conf.in"), read (data.getChildFile ("aes67.conf.in")));
        const auto before = data.getChildFile (juce::String (one.name)).getChildFile ("before");
        for (const auto& [relative, bytes] : filesBelow (before))
            write (home.getChildFile (juce::String (relative)), bytes);

        a3app::ProgramArgs args;
        args.role = a3app::Role::renderTruth;
        args.subcommand = one.argv;
        result = a3app::runSubcommand (args, platform);
    }
    ~RecordedRun() { home.deleteRecursively(); }

    std::string out() const { return withoutHome (result.out); }
    std::string err() const { return withoutHome (result.err); }

    std::string withoutHome (const std::string& text) const { return replaceAll (text, home.getFullPathName().toStdString(), placeholder); }

    // The files the command left, the package's template and the truth aside.
    std::map<std::string, std::string> left() const
    {
        auto all = filesBelow (home);
        all.erase (".local/share/a3-core/pipewire/pipewire-aes67.conf.in");
        return all;
    }

    fake::FakePlatform platform;
    juce::File home;
    a3app::SubcommandResult result;
};

void expectAsPython (const std::string& prefix, size_t expectedCases)
{
    const auto cases = casesNamed (prefix);
    EXPECT_EQ (cases.size(), expectedCases);
    for (const auto& one : cases)
    {
        const RecordedRun run (one);
        const auto recorded = data.getChildFile (juce::String (one.name));
        EXPECT_EQ (run.out(), read (recorded.getChildFile ("stdout"))) << one.name;
        EXPECT_EQ (withoutParserPosition (run.err()), withoutParserPosition (read (recorded.getChildFile ("stderr")))) << one.name;
        EXPECT_EQ (run.result.exitCode, one.exitCode) << one.name;
        EXPECT_EQ (run.left(), filesBelow (recorded.getChildFile ("after"))) << one.name;
    }
}
}

TEST (Subcommands, RenderUserAsPython)
{
    expectAsPython ("user-", 5);
}

TEST (Subcommands, NetworkFileOnlyOnce)
{
    expectAsPython ("network-file-", 3);
}

TEST (Subcommands, NetworkAsPython)
{
    // "network-file-*" is the cases above; these three print the defaults and write nothing
    const auto cases = casesNamed ("network-");
    ASSERT_EQ (cases.size(), 6u);
    for (const auto& one : cases)
    {
        if (one.name.rfind ("network-file-", 0) == 0)
            continue;
        const RecordedRun run (one);
        const auto recorded = data.getChildFile (juce::String (one.name));
        EXPECT_EQ (run.out(), read (recorded.getChildFile ("stdout"))) << one.name;
        EXPECT_EQ (withoutParserPosition (run.err()), withoutParserPosition (read (recorded.getChildFile ("stderr")))) << one.name;
        EXPECT_EQ (run.result.exitCode, 0) << one.name;
        EXPECT_EQ (run.left(), filesBelow (recorded.getChildFile ("after"))) << one.name;
    }
}

TEST (Subcommands, UnknownIsUsage)
{
    const auto cases = casesNamed ("usage-");
    ASSERT_EQ (cases.size(), 3u);
    for (const auto& one : cases)
    {
        const RecordedRun run (one);
        EXPECT_EQ (run.result.exitCode, 2) << one.name;
        EXPECT_EQ (one.exitCode, 2) << one.name;   // as B1's is
        EXPECT_EQ (run.result.out, "") << one.name;
        EXPECT_NE (run.result.err.find ("--render-truth user"), std::string::npos) << one.name;
        EXPECT_NE (run.result.err.find ("--render-truth network-file"), std::string::npos) << one.name;
        EXPECT_NE (run.result.err.find ("--render-truth network"), std::string::npos) << one.name;
        EXPECT_TRUE (run.left().empty()) << one.name;
    }
}

namespace
{
a3app::SubcommandResult ask (fake::FakePlatform& platform, std::initializer_list<const char*> words)
{
    a3app::ProgramArgs args;
    args.role = a3app::Role::truthQuery;
    for (const auto* word : words)
        args.subcommand.add (word);
    return a3app::runSubcommand (args, platform);
}

fake::FakePlatform shippedTruth()
{
    fake::FakePlatform platform;
    platform.env["A3_OSC_TRUTH"] = data.getChildFile ("a3-osc.json").getFullPathName().toStdString();
    return platform;
}
}

TEST (Subcommands, TruthHostAndPort)
{
    auto platform = shippedTruth();
    const auto host = ask (platform, { "host", "core" });
    EXPECT_EQ (host.exitCode, 0);
    EXPECT_EQ (host.out, "192.168.8.10\n");   // one value, and a newline: `read -r` takes it
    EXPECT_EQ (host.err, "");

    const auto port = ask (platform, { "port", "x11vnc", "vnc" });
    EXPECT_EQ (port.exitCode, 0);
    EXPECT_EQ (port.out, "5901\n");
    EXPECT_EQ (port.err, "");

    // the viewer recipe's two values from two questions
    EXPECT_EQ (ask (platform, { "port", "core", "osc" }).out, "9000\n");
}

TEST (Subcommands, AMissingValueIsOneAndSaysWhy)
{
    auto platform = shippedTruth();
    const auto host = ask (platform, { "host", "nobody" });
    EXPECT_EQ (host.exitCode, 1);
    EXPECT_EQ (host.out, "");
    EXPECT_EQ (host.err, "a3-core: no host 'nobody' in the truth\n");

    const auto port = ask (platform, { "port", "nobody", "osc" });
    EXPECT_EQ (port.exitCode, 1);
    EXPECT_EQ (port.out, "");
    EXPECT_EQ (port.err.rfind ("a3-core: ", 0), 0u);
    EXPECT_GT (port.err.size(), 10u);

    fake::FakePlatform nowhere;
    nowhere.env["A3_OSC_TRUTH"] = "/nonexistent/a3-osc.json";
    const auto none = ask (nowhere, { "host", "core" });
    EXPECT_EQ (none.exitCode, 1);
    EXPECT_EQ (none.out, "");
    EXPECT_EQ (none.err.rfind ("a3-core: ", 0), 0u);
}
