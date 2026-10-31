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
// One part of the system version: a submodule of the umbrella (a3-motion's as "a3-motion/a3-motion-ui"), or a firmware in
// one ("a3-motion firmware"), with the release tag of its pinned commit (empty when that commit is not here) and where it
// is published (its .gitmodules url; a firmware's folder in it; empty when no url is known).
struct Row
{
    juce::String name, tag, url;
    juce::String licences;   // its ONE licence, "GPL-3.0"; empty when not checked out or none is known
    bool noVersion = false;  // a part without a version of its own (docs, a hardware part without a REVISION file): its column stays empty
    bool operator== (const Row& other) const
    {
        return name == other.name && tag == other.tag && url == other.url && licences == other.licences;
    }
};

struct Facts
{
    bool found = false;     // a checkout with gitlinks at all
    juce::String version;   // the umbrella's own HEAD, as releaseText writes it
    juce::String licence;   // the umbrella's own code licence ("GPL-3.0")
    std::vector<Row> rows;
    bool operator== (const Facts& other) const { return found == other.found && version == other.version && licence == other.licence && rows == other.rows; }
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

// The licence of a repository's code: the first of its LICENSES/ folder's SPDX ids that is the project's own, shown short,
// in the order GPL-3.0 (code), CERN-OHL-S (hardware), CC-BY-SA-4.0 (documentation) -- so a3-doc, which only carries the
// last, says that. Left out: CC0-1.0 (configuration and trivial files), MIT, Apache, BSD, CC-BY-3.0, GPL-2.0, LicenseRef-*,
// the licences of vendored or third-party files. Empty when the folder is not there (the repository is not checked out).
juce::String codeLicence (const juce::File& repoDir);
// The parts of a repository read from its tree, not listed anywhere: "pcb" (the folder of a KiCad project under hardware/), "housing" (the folder under hardware/ holding FreeCAD,
// STL, 3MF or Blender files), each with the folder it is and, for the hardware, its REVISION (the file of that name in the
// folder, its first line; empty without one).
struct Part
{
    juce::String name, folder, revision;
};
std::vector<Part> partsOf (const juce::File& repoDir);

// One SPDX expression shown short ("GPL-3.0-or-later" -> "GPL-3.0", "CERN-OHL-S-2.0" -> "CERN-OHL-S"); "" for a licence
// that is not one of the project's own (see mainLicences).
juce::String shortLicence (const juce::String& spdxId);
// The licence of a folder of a repository (its firmware): the stanza of repoDir/.reuse/dep5 that covers it, the last one
// as REUSE says, a stanza for Markdown or ini files not counting; else the SPDX-License-Identifier of most of the source
// files under it; the first licence of an expression. Empty when neither says.
juce::String licenceOfFolder (const juce::File& repoDir, const juce::String& folder);

// One thing the system uses from others: its name, its version where known (a submodule's describe or short commit; JUCE's
// the one the program is built with), its licence as its author sets it (shown short, thirdPartyLicence), where it lives.
struct ThirdParty
{
    juce::String name, version, licence, url;
};
// Whether a submodule url is ours: published under github.com/a3-audio, a relative url (a sibling of ours), or none known.
// What is third-party is decided by this origin, not by the folder a submodule sits in (maintainer, 2026-10-31).
bool isOurs (const juce::String& url);
// A third party's SPDX expression as recorded, shortened as our licence column: "-or-later"/"-only" and the "LicenseRef-"
// namespace dropped ("AGPL-3.0-only OR LicenseRef-JUCE-9" -> "AGPL-3.0 OR JUCE-9").
juce::String thirdPartyLicence (const juce::String& expression);
// The third parties a repository's REUSE data names, in its order: every .reuse/dep5 Files stanza with a copyright holder
// who is not ours (ours: Raphael Eismann [and contributors], Patric Schmitz, A3 Audio), except stanzas of trivial files
// (CC0-1.0 only); named by its X-A3-Name (else its Comment's first sentence, else its first path), its licence its
// X-A3-License where the stanza mixes ours and theirs (else License), its url X-A3-Url, its version X-A3-Version (for
// what has no version git can read: kiss_fft "1.3.0", a copy or download by year and month, "2026-10"). Then the header's X-A3-Uses
// lines, "<name>; <licence>; <url>": what is built on without a file in the repository (JUCE). Empty without a dep5.
std::vector<ThirdParty> thirdPartyOf (const juce::File& repoDir);

// The node that closes the tree, and its entries' prefix: "third-party", "third-party/<name>".
inline constexpr const char* thirdPartyNode = "third-party";

// The checkout at `repo`: its HEAD's tag, each gitlink's pinned commit described in its submodule, the submodules of
// those at any depth and in any folder (`ls-tree -r`; a3-mixer's hardware/a3-kicad-lib, a3-stemdeck's a3-stem-separator),
// each named "<its repository's row>/<last path component>", and the firmware of a3-motion and a3-mixer as the tag of the
// last commit that changed it. A submodule inside a part's folder (pcb, housing, firmware, ui folder) is named under that
// part: "a3-mixer pcb/a3-kicad-lib". A submodule published outside a3-audio is no row: it and the third parties of every
// checked-out repository's REUSE data close the rows as the node "third-party", each entry once, by name.
Facts read (CommandRunner&, const juce::File& repo);

// A line of the PUBLISHED tree: which row, the name shown (a child's own part of its row name) and the connectors ahead.
struct TreeEntry
{
    size_t row = 0;
    juce::String name, prefix;
};
// The rows as a tree at any depth: "a/b" is b under a, "a/b/c" c under a/b, "a firmware" a part of a; a row whose parent
// is not there is a top.
std::vector<TreeEntry> treeOf (const std::vector<Row>& rows);
}
