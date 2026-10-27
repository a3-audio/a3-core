// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_osc_render (B0 f3345dc7feeb): the one truth written out for what cannot read JSON --
// x11vnc's port, the postinst's default network, the beat-analyzer's conf.d block and PipeWire's
// AES67 config. After the cut no Python runs on the rig (O12), so every file is byte-identical to
// the one the Python renderer wrote for the same truth. The hostname is a parameter: this library
// asks the machine nothing (the Platform seam supplies it).
#pragma once

#include <optional>
#include <string>

#include <juce_core/juce_core.h>

#include "Truth.h"

namespace a3core
{
// A file that cannot be written (the OSError the Python renderer let through).
struct RenderError
{
    std::string what;
};

// Where x11vnc and the screencast find their ports, relative to the home.
inline constexpr const char* envFilePath = ".config/a3/osc.env";
// The analyzer's file of ours: the whole file is a3-core's and the truth wins key by key.
inline constexpr const char* analyzerConfPath = ".config/beat-analyzer/conf.d/50-a3-osc.env";
// PipeWire's AES67 instance (a3-aes67.service).
inline constexpr const char* aes67ConfPath = ".config/pipewire/pipewire-aes67.conf";
// First line of every file we render; it is how a file of ours is told from one written by hand.
inline constexpr const char* renderedHeader = "# rendered from a3-osc.json by a3-osc-render -- edit the truth, not this file";

// OSC_VU_ for the meters, OSC_HOST_ for the clock.
std::string analyzerPrefix (const juce::var& route);

std::string oscEnv (const Truth&);           // the ports of x11vnc and the screencast
std::string networkDefaults (const Truth&);  // the INTERFACE=... lines the postinst feeds networkd
std::string analyzerBlock (const Truth&);

// The AES67 config for this machine, or nullopt without an audio socket. Throws RenderError if the
// template cannot be read.
std::optional<std::string> aes67Conf (const Truth&, const std::string& hostname, const juce::File& aes67Template);

bool isRendered (const juce::File&);

// Render the AES67 config into PipeWire's folder; what happened, said. A config written by hand is
// never replaced; without an audio socket a rendered one goes.
std::string writeAes67Conf (const Truth&, const juce::File& home, const std::string& hostname, const juce::File& aes67Template);

// osc.env, the analyzer's conf.d file and the AES67 config; the AES67 line is returned, to be said.
std::string writeUserFiles (const Truth&, const juce::File& home, const std::string& hostname, const juce::File& aes67Template);

// Swap the new file in whole through a hidden temporary name (the analyzer's conf.d skips it).
void replaceWhole (const juce::File&, const std::string& text);

// ~/.config/a3/network.json as the installer first writes it.
std::string networkFile (const Truth&);

// Create the maintainer's network file; never overwrite it. True if it was written.
bool writeNetworkFileOnce (const Truth&, const juce::File& path);

// The line a3-osc-render said on stderr for a refused network file, or nullopt.
std::optional<std::string> refusedNetworkNotice (const Truth&);
}
