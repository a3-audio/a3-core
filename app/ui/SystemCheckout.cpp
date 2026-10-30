// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemCheckout.h"

namespace a3app::checkout
{
namespace
{
// Where each device's firmware lives in its repository (a3-motion README, a3-mixer README).
struct FirmwareDir
{
    const char* submodule;
    const char* path;
};
constexpr FirmwareDir firmwareDirs[] { { "a3-mixer", "hardware/mainboard/firmware" }, { "a3-motion", "firmware" } };

bool isHex (const juce::String& text) { return text.isNotEmpty() && text.containsOnly ("0123456789abcdef"); }
bool isNumber (const juce::String& text) { return text.isNotEmpty() && text.containsOnly ("0123456789"); }

// The output of a git command that succeeded; nothing when it failed (its complaint is no answer).
juce::String gitOutput (CommandRunner& runner, const juce::File& dir, std::initializer_list<juce::String> args)
{
    Command argv { "git", "-C", dir.getFullPathName().toStdString() };
    for (const auto& arg : args)
        argv.push_back (arg.toStdString());
    const auto result = runner.run (argv);
    return result.exitCode == 0 ? juce::String (result.output).trim() : juce::String();
}

juce::String tagOf (CommandRunner& runner, const juce::File& dir, const juce::String& commit)
{
    return releaseText (gitOutput (runner, dir, { "describe", "--tags", "--match", "v*", "--always", commit }));
}

const FirmwareDir* firmwareOf (const juce::String& submodule)
{
    for (const auto& dir : firmwareDirs)
        if (submodule == dir.submodule)
            return &dir;
    return nullptr;
}

// A submodule's row, then its firmware's, then its own submodules' (one level: a3-motion's ui).
void addSubmodule (CommandRunner& runner, const juce::File& repo, const Gitlink& link, Facts& facts)
{
    const auto dir = repo.getChildFile (link.path);
    facts.rows.push_back ({ link.path, tagOf (runner, dir, link.commit) });
    if (facts.rows.back().tag.isEmpty())
        return;   // not checked out: nothing more to ask it
    if (const auto* firmware = firmwareOf (link.path))
    {
        const auto changed = gitOutput (runner, dir, { "log", "-1", "--format=%H", link.commit, "--", firmware->path });
        facts.rows.push_back ({ link.path + " firmware", changed.isNotEmpty() ? tagOf (runner, dir, changed) : juce::String() });
    }
    for (const auto& inner : parseGitlinks (gitOutput (runner, dir, { "ls-tree", link.commit })))
        facts.rows.push_back ({ link.path + "/" + inner.path, tagOf (runner, dir.getChildFile (inner.path), inner.commit) });
}
}

std::vector<Gitlink> parseGitlinks (const juce::String& lsTree)
{
    std::vector<Gitlink> links;
    for (const auto& line : juce::StringArray::fromLines (lsTree))
    {
        const auto fields = juce::StringArray::fromTokens (line.upToFirstOccurrenceOf ("\t", false, false), " ", {});
        const auto path = line.fromFirstOccurrenceOf ("\t", false, false).trim();
        if (fields.size() == 3 && fields[0] == "160000" && fields[1] == "commit" && path.isNotEmpty())
            links.push_back ({ path, fields[2] });
    }
    return links;
}

// "<tag>-<n>-g<commit>": read from the right, a tag may hold dashes of its own.
juce::String releaseText (const juce::String& describe)
{
    const auto text = describe.trim();
    const auto parts = juce::StringArray::fromTokens (text, "-", {});
    const int n = parts.size();
    if (n >= 3 && parts[n - 1].startsWithChar ('g') && isHex (parts[n - 1].substring (1)) && isNumber (parts[n - 2]))
        return juce::StringArray (parts.begin(), n - 2).joinIntoString ("-") + "+" + parts[n - 2];
    return text;
}

juce::String tagText (const Row& row) { return row.tag.isNotEmpty() ? row.tag : juce::String ("not checked out"); }

Facts read (CommandRunner& runner, const juce::File& repo)
{
    Facts facts;
    const auto links = parseGitlinks (gitOutput (runner, repo, { "ls-tree", "HEAD" }));
    if (links.empty())
        return facts;
    facts.found = true;
    facts.version = releaseText (gitOutput (runner, repo, { "describe", "--tags", "--match", "v*", "--always", "HEAD" }));
    for (const auto& link : links)
        addSubmodule (runner, repo, link, facts);
    return facts;
}
}
