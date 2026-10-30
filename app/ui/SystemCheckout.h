// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The A³ System card: which system version this machine runs, read from the umbrella checkout (~/a3-system) by git,
// as the Canvas's System/SystemRepo read it (a3-system canvas/Source). Reads only; git runs through a CommandRunner
// (posix_spawn, never fork) off the message thread.
#pragma once
#include "CommandRunner.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace a3app::checkout
{
// One part of the system version: a submodule of the umbrella (a3-motion's ui as "a3-motion/ui"), or a firmware in
// one ("a3-motion firmware"), with the release tag of its pinned commit; empty when that commit is not here.
struct Row
{
    juce::String name, tag;
    bool operator== (const Row& other) const { return name == other.name && tag == other.tag; }
};

struct Facts
{
    bool found = false;     // a checkout with gitlinks at all
    juce::String version;   // the umbrella's own HEAD, as releaseText writes it
    std::vector<Row> rows;
    bool operator== (const Facts& other) const { return found == other.found && version == other.version && rows == other.rows; }
};

struct Gitlink
{
    juce::String path, commit;
};

// `git ls-tree` output: the entries of mode 160000 (submodules), in git's order.
std::vector<Gitlink> parseGitlinks (const juce::String& lsTree);
// `git describe --tags --always` as a version: "v03.0" on the tag, "v03.0+413" 413 commits after it, the short commit
// when no tag precedes it.
juce::String releaseText (const juce::String& describe);
// What the card writes for a row: its tag, or "not checked out".
juce::String tagText (const Row&);

// The checkout at `repo`: its HEAD's tag, each gitlink's pinned commit described in its submodule, a3-motion's own
// submodules, and the firmware of a3-motion and a3-mixer as the tag of the last commit that changed it.
Facts read (CommandRunner&, const juce::File& repo);
}
