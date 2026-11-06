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
#include <vector>

#include <juce_core/juce_core.h>

#include "Aes67Streams.h"
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
// streamsSignature() of the streams last rendered into it: what a later start compares against.
inline constexpr const char* aes67SignaturePath = ".local/state/a3-core/aes67-streams.sig";
// First line of every file we render; it is how a file of ours is told from one written by hand.
inline constexpr const char* renderedHeader = "# rendered from a3-osc.json by a3-osc-render -- edit the truth, not this file";

// A route of the meters. The analyzer has none of its own since O13 (the engine measures /vu), so
// one from it in an older truth renders no line.
bool carriesVu (const juce::var& route);

std::string oscEnv (const Truth&);           // the ports of x11vnc and the screencast
std::string networkDefaults (const Truth&);  // the INTERFACE=... lines the postinst feeds networkd
std::string analyzerBlock (const Truth&);

// The AES67 config for this machine, or nullopt without an audio socket: one rtp-sink block per stream,
// in their order, where the template's @RTP_SINKS@ line stands. The block comes from the sibling
// template <name>-sink.conf.in (pipewire-aes67.conf.in: pipewire-aes67-sink.conf.in). Throws
// RenderError if a template cannot be read or the main one has no @RTP_SINKS@ line.
std::optional<std::string> aes67Conf (const Truth&, const std::string& hostname, const juce::File& aes67Template,
                                      const std::vector<StreamSpec>& streams);

bool isRendered (const juce::File&);

// Render the AES67 config into PipeWire's folder; what happened, said. A config written by hand is
// never replaced; without an audio socket a rendered one goes. After a write the streams' signature
// is stored at aes67SignaturePath.
std::string writeAes67Conf (const Truth&, const juce::File& home, const std::string& hostname, const juce::File& aes67Template,
                            const std::vector<StreamSpec>& streams);

// The AES67 line as said: `written` (writeAes67Conf's), and why only the legacy stream was rendered
// when the speaker layout could not be read (`legacyOnlyBecause` not empty).
std::string aes67Said (const std::string& written, const std::string& legacyOnlyBecause);

// osc.env, the analyzer's conf.d file and the AES67 config; the AES67 line is returned, to be said.
std::string writeUserFiles (const Truth&, const juce::File& home, const std::string& hostname, const juce::File& aes67Template,
                            const std::vector<StreamSpec>& streams);

// Swap the new file in whole through a hidden temporary name (the analyzer's conf.d skips it).
void replaceWhole (const juce::File&, const std::string& text);

// ~/.config/a3/network.json as the installer first writes it.
std::string networkFile (const Truth&);

// Create the maintainer's network file; never overwrite it. True if it was written.
bool writeNetworkFileOnce (const Truth&, const juce::File& path);

// The line a3-osc-render said on stderr for a refused network file, or nullopt.
std::optional<std::string> refusedNetworkNotice (const Truth&);
}
