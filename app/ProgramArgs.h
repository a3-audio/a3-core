// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The one program's command line: which role runs, and that role's flags. Every role has its own
// flags; another role's flag, an unknown one or a missing value is refused (exit 2 with the usage).
//
//   a3-core                                   the window
//   a3-core --headless                        the supervisor: Core and the engine as children
//   a3-core --role core|engine|window|screencast-fanout|i3-bar|clock [flags]
//   a3-core --render-truth user|network-file|network
//   a3-core --truth host NAME | --truth port PROGRAM ROLE
//   a3-core --links
#pragma once

#include <optional>
#include <string>
#include <variant>

#include <juce_core/juce_core.h>

#include "AppArgs.h"
#include "CoreService.h"
#include "Platform.h"

namespace a3app
{
enum class Role { window, core, engine, screencastFanout, i3Bar, supervisor, renderTruth, truthQuery, links, clock };

// The core flags that default to the truth (B1's argparse defaults read a3-osc.json); unset here
// means "the truth's", which only coreOptions() can know.
struct CoreGiven
{
    std::optional<std::string> ip;
    std::optional<int> port;
    std::optional<a3core::Endpoint> mixer, motion;
};

struct ProgramArgs
{
    Role role = Role::window;
    a3core::ServiceOptions core;   // the flags given; see coreOptions() for the rest
    CoreGiven coreGiven;
    AppArgs engine;
    std::optional<int> fanoutPort; // not given: A3_SCREENCAST_PORT (see fanoutPort())
    double fanoutLinger = 10.0;
    juce::StringArray encoder;     // the fan-out's command, after --
    juce::StringArray subcommand;  // --render-truth's or --truth's words
    std::string clockDevice;                 // the PTP hardware clock to steer; empty: the audio port's
    std::string clockScarlett = "alsa_input.usb-Focusrite_Scarlett_18i20";   // the capture node (node.name prefix)
    bool clockObserve = false;               // the clock role measures and never writes
};

extern const char* const usage;

// The word after --role for a role that has one (core, engine, ...); empty for the others.
std::string roleWord (Role);

std::optional<ProgramArgs> parseProgramArgs (const juce::StringArray&, juce::String& error);

// B1's a3_osc.truth_path and network_path, from the environment through the platform.
struct CoreFiles
{
    juce::File truth;
    std::optional<juce::File> network;
};
CoreFiles coreFiles (const a3core::Platform&);

// The core role's ServiceOptions as B1's main block had them: the flags given, every other default
// from the truth (--ip: the truth's `any`, --port: core.osc, --mixer/--motion: their osc listeners),
// the files from the environment, the places and the host name from the platform. A truth that does
// not load is the refusal CoreService would give ("a3-core: ...").
std::variant<a3core::ServiceOptions, std::string> coreOptions (const ProgramArgs&, const a3core::Platform&);

// The fan-out's port: --port, else A3_SCREENCAST_PORT (B1's default), else 0.
int fanoutPort (const ProgramArgs&, const a3core::Platform&);
// B1's parser.error for a port of 0 or no command; nullopt when the fan-out may start.
std::optional<std::string> fanoutRefusal (int port, const juce::StringArray& command);
}
