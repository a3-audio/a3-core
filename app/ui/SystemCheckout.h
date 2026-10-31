// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The A³ System card: which system version this machine runs, read from the umbrella checkout (~/a3-system) by git,
// as the Canvas's System/SystemRepo read it (a3-system canvas/Source). Reads only; git runs through a CommandRunner
// (posix_spawn, never fork) off the message thread.
#pragma once
#include "CommandRunner.h"
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace a3app::checkout
{
// One part of the system version: a submodule of the umbrella (a3-motion's ui as "a3-motion/ui"), or a firmware in
// one ("a3-motion firmware"), with the release tag of its pinned commit (empty when that commit is not here) and where it
// is published (its .gitmodules url; a firmware's folder in it; empty when no url is known).
struct Row
{
    juce::String name, tag, url;
    juce::String licences;   // its main licences, "GPL-3.0 · CERN-OHL-S"; empty when not checked out or none is known
    bool operator== (const Row& other) const
    {
        return name == other.name && tag == other.tag && url == other.url && licences == other.licences;
    }
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

// `git config --get-regexp ^submodule\..*\.url$` output: submodule name -> url, a relative one ("../x.git") resolved against
// `base` (the repository's own url), ".git" dropped.
std::map<juce::String, juce::String> parseUrls (const juce::String& output, const juce::String& base);

// `git ls-tree` output: the entries of mode 160000 (submodules), in git's order.
std::vector<Gitlink> parseGitlinks (const juce::String& lsTree);
// `git describe --tags --always` as a version: "v03.0" on the tag, "v03.0+413" 413 commits after it, the short commit
// when no tag precedes it.
juce::String releaseText (const juce::String& describe);
// What the card writes for a row: its tag, or "not checked out".
juce::String tagText (const Row&);

// The licences a repository carries as its own, from the SPDX ids of its LICENSES/ folder (REUSE), shown short: the code's
// GPL-3.0, the hardware's CERN-OHL-S, the documentation's CC-BY-SA-4.0, in that order. Left out: CC0-1.0 (configuration and
// trivial files), MIT, Apache, BSD, CC-BY-3.0, GPL-2.0, LicenseRef-* -- licences of vendored or third-party files, not of
// the project's work. Empty when the folder is not there (the repository is not checked out).
juce::String mainLicences (const juce::File& repoDir);
// One SPDX expression shown short ("GPL-3.0-or-later" -> "GPL-3.0", "CERN-OHL-S-2.0" -> "CERN-OHL-S"); "" for a licence
// that is not one of the project's own (see mainLicences).
juce::String shortLicence (const juce::String& spdxId);
// The licence of a folder of a repository (its firmware): the stanza of repoDir/.reuse/dep5 that covers it, the last one
// as REUSE says, a stanza for Markdown or ini files not counting; else the SPDX-License-Identifier of most of the source
// files under it. Empty when neither says.
juce::String licenceOfFolder (const juce::File& repoDir, const juce::String& folder);

// The checkout at `repo`: its HEAD's tag, each gitlink's pinned commit described in its submodule, a3-motion's own
// submodules, and the firmware of a3-motion and a3-mixer as the tag of the last commit that changed it.
Facts read (CommandRunner&, const juce::File& repo);
}
