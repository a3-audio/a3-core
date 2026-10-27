// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Subcommands.h"

#include "Render.h"
#include "Truth.h"

namespace a3app
{
namespace
{
SubcommandResult refused (const std::string& what) { return { 1, "", "a3-core: " + what + "\n" }; }

// The truth as B1's a3_osc.load() reads it, or why it cannot be.
std::variant<a3core::Truth, SubcommandResult> loadTruth (const a3core::Platform& platform)
{
    const auto files = coreFiles (platform);
    auto loaded = a3core::Truth::load (files.truth, files.network);
    if (const auto* problem = std::get_if<a3core::TruthError> (&loaded))
        return refused (problem->what);
    return std::move (std::get<a3core::Truth> (loaded));
}

SubcommandResult render (const juce::StringArray& words, const a3core::Platform& platform)
{
    auto loaded = loadTruth (platform);
    if (auto* failure = std::get_if<SubcommandResult> (&loaded))
        return *failure;
    const auto& truth = std::get<a3core::Truth> (loaded);

    SubcommandResult result;
    // Said for every subcommand: the postinst's network render would otherwise configure the interface
    // from the package's values without a word about the maintainer's file.
    if (const auto notice = a3core::refusedNetworkNotice (truth))
        result.err = *notice;

    try
    {
        if (words == juce::StringArray ("user"))
            result.out = "a3-osc-render: " + a3core::writeUserFiles (truth, platform.home(), platform.hostname(),
                                                                       platform.packageShare().getChildFile ("pipewire/pipewire-aes67.conf.in")) + "\n";
        else if (words == juce::StringArray ("network-file"))
        {
            const auto path = platform.configDir().getChildFile ("network.json");
            const bool made = a3core::writeNetworkFileOnce (truth, path);
            result.out = path.getFullPathName().toStdString() + ": " + (made ? "created" : "kept (the maintainer's)") + "\n";
        }
        else if (words == juce::StringArray ("network"))
            result.out = a3core::networkDefaults (truth);
        else   // the argument parser refuses an unknown word first; this is for a caller that did not ask it
        {
            result.err += usage;
            result.exitCode = 2;
        }
    }
    catch (const a3core::RenderError& problem)
    {
        result.err += "a3-core: " + problem.what + "\n";
        result.exitCode = 1;
    }
    return result;
}

// One value and a newline, for a script's `read`; a fact the truth lacks is exit 1 and why.
SubcommandResult query (const juce::StringArray& words, const a3core::Platform& platform)
{
    auto loaded = loadTruth (platform);
    if (auto* failure = std::get_if<SubcommandResult> (&loaded))
        return *failure;
    const auto& truth = std::get<a3core::Truth> (loaded);
    try
    {
        if (words.size() == 2 && words[0] == "host")
            return { 0, truth.host (words[1].toStdString()) + "\n", "" };
        if (words.size() == 3 && words[0] == "port")
            return { 0, std::to_string (truth.port (words[1].toStdString(), words[2].toStdString())) + "\n", "" };
    }
    catch (const a3core::TruthError& missing)
    {
        return refused (missing.what);
    }
    return { 2, "", usage };
}
}

SubcommandResult runSubcommand (const ProgramArgs& args, const a3core::Platform& platform)
{
    return args.role == Role::truthQuery ? query (args.subcommand, platform) : render (args.subcommand, platform);
}
}
