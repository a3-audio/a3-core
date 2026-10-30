// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProgramArgs.h"

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>

#include "Truth.h"

namespace a3app
{
const char* const usage =
    "usage: a3-core                      the window\n"
    "       a3-core --headless           Core and the engine as children, without a window\n"
    "       a3-core --role core [--ip HOST] [--port N] [--mixer HOST:PORT] [--motion HOST:PORT]\n"
    "                           [--subscriber NAME=HOST:PORT]... [--print-osc] [--web-bind HOST:PORT] [--no-web]\n"
    "       a3-core --role engine [--config FILE] [--spatial DIR] [--truth FILE] [--no-connect] [--timing]\n"
    "                             [--check-config]\n"
    "       a3-core --role window\n"
    "       a3-core --role screencast-fanout --port N [--linger SECONDS] -- COMMAND...\n"
    "       a3-core --role i3-bar\n"
    "       a3-core --role clock [--device PATH] [--scarlett NODE] [--observe]\n"
    "       a3-core --render-truth user|network-file|network\n"
    "       a3-core --truth host NAME | --truth port PROGRAM ROLE\n"
    "       a3-core --links              the JACK/PipeWire connections, explained\n"
    "  core: every default from the truth (A3_OSC_TRUTH, else /usr/share/a3/a3-osc.json)\n"
    "  engine: --no-window and --service are accepted and do nothing (the engine role has no window)\n"
    "  screencast-fanout: --port defaults to A3_SCREENCAST_PORT\n"
    "  clock: steers the audio port's PTP clock from the Scarlett's capture node; --observe only measures\n";

namespace
{
struct Flag
{
    const char* name;
    bool takesValue;
};

const std::vector<Flag> coreFlags { { "--ip", true }, { "--port", true }, { "--mixer", true }, { "--motion", true },
                                    { "--subscriber", true }, { "--print-osc", false }, { "--web-bind", true },
                                    { "--no-web", false } };
const std::vector<Flag> engineFlags { { "--config", true }, { "--spatial", true }, { "--truth", true }, { "--no-connect", false },
                                      { "--timing", false }, { "--check-config", false }, { "--no-window", false },
                                      { "--service", false } };
const std::vector<Flag> fanoutFlags { { "--port", true }, { "--linger", true } };
const std::vector<Flag> clockFlags { { "--device", true }, { "--scarlett", true }, { "--observe", false } };
const std::vector<Flag> noFlags;

const char* const dashes = "--";
const char* const fanoutNeeds = "needs --port (or A3_SCREENCAST_PORT) and -- <encoder command>";

struct RoleName
{
    Role role;
    const char* name;
};
const std::vector<RoleName> roleNames { { Role::core, "core" }, { Role::engine, "engine" }, { Role::window, "window" },
                                        { Role::screencastFanout, "screencast-fanout" }, { Role::i3Bar, "i3-bar" },
                                        { Role::clock, "clock" } };
const juce::StringArray renderTargets { "user", "network-file", "network" };

const std::vector<Flag>& flagsOf (Role role)
{
    switch (role)
    {
        case Role::core: return coreFlags;
        case Role::engine: return engineFlags;
        case Role::screencastFanout: return fanoutFlags;
        case Role::clock: return clockFlags;
        case Role::window:
        case Role::i3Bar:
        case Role::supervisor:
        case Role::renderTruth:
        case Role::truthQuery:
        case Role::links: return noFlags;
    }
    return noFlags;
}

juce::String phraseOf (Role role)
{
    switch (role)
    {
        case Role::window: return "the window";
        case Role::supervisor: return "--headless";
        case Role::renderTruth: return "--render-truth";
        case Role::truthQuery: return "--truth host|port";
        case Role::links: return "--links";
        case Role::core:
        case Role::engine:
        case Role::screencastFanout:
        case Role::i3Bar:
        case Role::clock: break;
    }
    return "--role " + roleWord (role);
}

const Flag* find (const std::vector<Flag>& flags, const juce::String& word)
{
    for (const auto& flag : flags)
        if (word == flag.name)
            return &flag;
    return nullptr;
}

bool anyRoleKnows (const juce::String& word)
{
    for (const auto* flags : { &coreFlags, &engineFlags, &fanoutFlags, &clockFlags })
        if (find (*flags, word) != nullptr)
            return true;
    return word == dashes;
}

std::optional<int> parseInt (const juce::String& text)
{
    const auto raw = text.toStdString();
    if (raw.empty())
        return std::nullopt;
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol (raw.c_str(), &end, 10);
    if (*end != '\0' || errno != 0 || value < INT_MIN || value > INT_MAX || std::isspace (static_cast<unsigned char> (raw[0])))
        return std::nullopt;
    return static_cast<int> (value);
}

std::optional<double> parseDouble (const juce::String& text)
{
    const auto raw = text.toStdString();
    if (raw.empty() || std::isspace (static_cast<unsigned char> (raw[0])))
        return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod (raw.c_str(), &end);
    if (*end != '\0' || ! std::isfinite (value))
        return std::nullopt;
    return value;
}

// B1's `spec.rpartition(":")`, then int() of the port.
std::optional<a3core::Endpoint> parseEndpoint (const juce::String& text)
{
    const int colon = text.lastIndexOfChar (':');
    if (colon < 0)
        return std::nullopt;
    const auto port = parseInt (text.substring (colon + 1));
    if (! port)
        return std::nullopt;
    return a3core::Endpoint { text.substring (0, colon).toStdString(), *port };
}

std::optional<Role> roleNamed (const juce::String& name)
{
    for (const auto& known : roleNames)
        if (name == known.name)
            return known.role;
    return std::nullopt;
}

// The words that choose the role (--role, --headless, --render-truth), taken out of `rest`.
struct Selection
{
    std::optional<Role> role;
    juce::String chosenBy;
    juce::StringArray subcommand;
    juce::StringArray rest;
};

std::optional<Selection> selectRole (const juce::StringArray& words, juce::String& error)
{
    Selection selection;
    const auto choose = [&] (Role role, const juce::String& by) {
        if (selection.role)
        {
            error = by == selection.chosenBy ? by + " is given twice" : selection.chosenBy + " cannot be combined with " + by;
            return false;
        }
        selection.role = role;
        selection.chosenBy = by;
        return true;
    };
    for (int i = 0; i < words.size(); ++i)
    {
        const auto& word = words[i];
        if (word == dashes)   // the rest is a command, whatever it says
        {
            for (; i < words.size(); ++i)
                selection.rest.add (words[i]);
            break;
        }
        if (word == "--headless" || word == "--links")
        {
            if (! choose (word == "--links" ? Role::links : Role::supervisor, word))
                return std::nullopt;
            continue;
        }
        if (word != "--role" && word != "--render-truth")
        {
            selection.rest.add (word);
            continue;
        }
        if (i + 1 >= words.size())
        {
            error = word + " needs a value";
            return std::nullopt;
        }
        const auto& value = words[++i];
        if (word == "--role")
        {
            const auto role = roleNamed (value);
            if (! role)
            {
                error = "--role: no role " + value + " (core, engine, window, screencast-fanout, i3-bar or clock)";
                return std::nullopt;
            }
            if (! choose (*role, word))
                return std::nullopt;
        }
        else
        {
            if (! renderTargets.contains (value))
            {
                error = "--render-truth: nothing called " + value + " to render (user, network-file or network)";
                return std::nullopt;
            }
            if (! choose (Role::renderTruth, word))
                return std::nullopt;
            selection.subcommand = juce::StringArray (value);
        }
    }
    return selection;
}

// `--truth host NAME` and `--truth port PROGRAM ROLE` as the whole command line.
bool isTruthQuery (const juce::StringArray& rest) { return rest.size() >= 2 && rest[0] == "--truth" && (rest[1] == "host" || rest[1] == "port"); }

std::optional<juce::StringArray> truthQuery (const juce::StringArray& rest, juce::String& error)
{
    const int wanted = rest[1] == "host" ? 3 : 4;
    if (rest.size() != wanted)
    {
        error = "--truth host NAME | --truth port PROGRAM ROLE";
        return std::nullopt;
    }
    juce::StringArray words (rest);
    words.remove (0);
    return words;
}

// Every word is one of the role's flags, and a value flag has its value.
bool checkFlags (Role role, const juce::StringArray& rest, juce::String& error)
{
    const auto& flags = flagsOf (role);
    for (int i = 0; i < rest.size(); ++i)
    {
        const auto& word = rest[i];
        if (role == Role::screencastFanout && word == dashes)
            return true;
        const auto* flag = find (flags, word);
        if (flag == nullptr)
        {
            error = anyRoleKnows (word) ? word + " is not a flag of " + phraseOf (role) : "unknown argument " + word;
            return false;
        }
        if (flag->takesValue && ++i >= rest.size())
        {
            error = word + " needs a value";
            return false;
        }
    }
    return true;
}

bool fillCore (ProgramArgs& args, const juce::StringArray& rest, juce::String& error)
{
    for (int i = 0; i < rest.size(); ++i)
    {
        const auto& word = rest[i];
        if (word == "--print-osc")
            args.core.core.printOsc = true;
        else if (word == "--no-web")
            args.core.noWeb = true;
        else
        {
            const auto& value = rest[++i];
            if (word == "--ip")
                args.coreGiven.ip = value.toStdString();
            else if (word == "--web-bind")
                args.core.webBind = value.toStdString();
            else if (word == "--subscriber")
                args.core.core.subscribers.push_back (value.toStdString());
            else if (word == "--port")
            {
                args.coreGiven.port = parseInt (value);
                if (! args.coreGiven.port)
                {
                    error = "--port: not a whole number: " + value;
                    return false;
                }
            }
            else
            {
                const auto endpoint = parseEndpoint (value);
                if (! endpoint)
                {
                    error = word + ": not HOST:PORT: " + value;
                    return false;
                }
                (word == "--mixer" ? args.coreGiven.mixer : args.coreGiven.motion) = endpoint;
            }
        }
    }
    return true;
}

bool fillFanout (ProgramArgs& args, const juce::StringArray& rest, juce::String& error)
{
    for (int i = 0; i < rest.size(); ++i)
    {
        const auto& word = rest[i];
        if (word == dashes)
        {
            for (++i; i < rest.size(); ++i)
                args.encoder.add (rest[i]);
            return true;
        }
        const auto& value = rest[++i];
        if (word == "--port")
        {
            args.fanoutPort = parseInt (value);
            if (! args.fanoutPort)
            {
                error = "--port: not a whole number: " + value;
                return false;
            }
        }
        else
        {
            const auto linger = parseDouble (value);
            if (! linger)
            {
                error = "--linger: not a number: " + value;
                return false;
            }
            args.fanoutLinger = *linger;
        }
    }
    return true;
}

void fillClock (ProgramArgs& args, const juce::StringArray& rest)
{
    for (int i = 0; i < rest.size(); ++i)
    {
        if (rest[i] == "--observe")
            args.clockObserve = true;
        else if (rest[i] == "--scarlett")
            args.clockScarlett = rest[++i].toStdString();   // checkFlags made sure it has a value
        else
            args.clockDevice = rest[++i].toStdString();
    }
}

bool fillRole (ProgramArgs& args, const juce::StringArray& rest, juce::String& error)
{
    switch (args.role)
    {
        case Role::core: return fillCore (args, rest, error);
        case Role::clock:
            fillClock (args, rest);
            return true;
        case Role::screencastFanout: return fillFanout (args, rest, error);
        case Role::engine:
        {
            const auto engine = parseAppArgs (rest, error);   // Plan 4's own parser
            if (engine)
                args.engine = *engine;
            return engine.has_value();
        }
        case Role::window:
        case Role::i3Bar:
        case Role::supervisor:
        case Role::renderTruth:
        case Role::truthQuery:
        case Role::links: return true;
    }
    return true;
}

const char* nullIfUnset (const std::optional<std::string>& value) { return value ? value->c_str() : nullptr; }
}

std::string roleWord (Role role)
{
    for (const auto& known : roleNames)
        if (known.role == role)
            return known.name;
    return {};
}

std::optional<ProgramArgs> parseProgramArgs (const juce::StringArray& words, juce::String& error)
{
    const auto selection = selectRole (words, error);
    if (! selection)
        return std::nullopt;

    ProgramArgs args;
    juce::StringArray rest = selection->rest;
    if (selection->role)
    {
        args.role = *selection->role;
        args.subcommand = selection->subcommand;
    }
    else if (isTruthQuery (rest))
    {
        const auto query = truthQuery (rest, error);
        if (! query)
            return std::nullopt;
        args.role = Role::truthQuery;
        args.subcommand = *query;
        rest.clear();
    }

    if (! checkFlags (args.role, rest, error) || ! fillRole (args, rest, error))
        return std::nullopt;
    return args;
}

CoreFiles coreFiles (const a3core::Platform& platform)
{
    const auto envTruth = platform.environment ("A3_OSC_TRUTH");
    const auto envNetwork = platform.environment ("A3_NETWORK");
    const bool truthGiven = envTruth && ! envTruth->empty();
    return { a3core::truthPath (std::nullopt, nullIfUnset (envTruth)),
             a3core::networkPath (truthGiven, platform.home(), nullIfUnset (envNetwork)) };
}

std::variant<a3core::ServiceOptions, std::string> coreOptions (const ProgramArgs& args, const a3core::Platform& platform)
{
    const auto files = coreFiles (platform);
    const auto loaded = a3core::Truth::load (files.truth, files.network);
    if (const auto* refused = std::get_if<a3core::TruthError> (&loaded))
        return "a3-core: " + refused->what;
    const auto& truth = std::get<a3core::Truth> (loaded);

    auto options = args.core;
    const auto& given = args.coreGiven;
    options.core.ip = given.ip.value_or (truth.host ("any"));
    options.core.port = given.port.value_or (truth.port ("core", "osc"));
    options.core.mixer = given.mixer.value_or (truth.endpoint ("mixer", "osc"));
    options.core.motion = given.motion.value_or (truth.endpoint ("motion", "osc"));
    options.home = platform.home();
    options.stateDir = platform.stateDir().getParentDirectory();   // Core keeps its files in <this>/a3-core
    options.packageShare = platform.packageShare();
    options.hostname = platform.hostname();
    options.truthPath = files.truth;
    options.networkPath = files.network;
    return options;
}

int fanoutPort (const ProgramArgs& args, const a3core::Platform& platform)
{
    if (args.fanoutPort)
        return *args.fanoutPort;
    const auto fromEnvironment = platform.environment ("A3_SCREENCAST_PORT");
    if (! fromEnvironment)
        return 0;
    return parseInt (juce::String (*fromEnvironment)).value_or (0);
}

std::optional<std::string> fanoutRefusal (int port, const juce::StringArray& command)
{
    if (port == 0 || command.isEmpty())
        return std::string (fanoutNeeds);
    return std::nullopt;
}
}
