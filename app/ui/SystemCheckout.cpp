// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemCheckout.h"
#include <algorithm>
#include <cstring>
#include <set>

namespace a3app::checkout
{
namespace
{
// The devices that carry a firmware, and where: top level firmware/ in both (a3-mixer laid out like a3-motion, 2026-10-31).
constexpr const char* firmwareSubmodules[] { "a3-mixer", "a3-motion" };
constexpr const char* firmwareFolder = "firmware";
constexpr const char* uiFolder = "ui";

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

// Each submodule's url by its path: a section's name need not be its path (a submodule inside a folder); without a path
// entry the name is taken as the path.
std::map<juce::String, juce::String> urlsAt (CommandRunner& runner, const juce::File& dir, const juce::String& commit, const juce::String& base)
{
    const auto blob = commit + ":.gitmodules";
    auto byName = parseUrls (gitOutput (runner, dir, { "config", "--blob", blob, "--get-regexp", "^submodule\\..*\\.url$" }), base);
    for (const auto& line : juce::StringArray::fromLines (gitOutput (runner, dir, { "config", "--blob", blob, "--get-regexp", "^submodule\\..*\\.path$" })))
    {
        const auto name = line.upToFirstOccurrenceOf (" ", false, false).fromFirstOccurrenceOf ("submodule.", false, false)
                              .upToLastOccurrenceOf (".path", false, false);
        const auto path = line.fromFirstOccurrenceOf (" ", false, false).trim();
        const auto found = byName.find (name);
        if (path.isNotEmpty() && name != path && found != byName.end())
            byName[path] = found->second;
    }
    return byName;
}

juce::String lastComponent (const juce::String& path) { return path.fromLastOccurrenceOf ("/", false, false); }

juce::String urlOf (const std::map<juce::String, juce::String>& urls, const juce::String& name)
{
    const auto found = urls.find (name);
    return found != urls.end() ? found->second : juce::String();
}

juce::String tagOf (CommandRunner& runner, const juce::File& dir, const juce::String& commit)
{
    return releaseText (gitOutput (runner, dir, { "describe", "--tags", "--match", "v*", "--always", commit }));
}

}

juce::String shortLicence (const juce::String& id)
{
    for (const auto* prefix : { "GPL-3.0", "AGPL-3.0" })
        if (id.startsWith (prefix))
            return prefix;
    if (id.startsWith ("CERN-OHL-"))
        return id.upToLastOccurrenceOf ("-", false, false);
    if (id.startsWith ("CC-BY-SA-"))
        return id;
    return {};
}

namespace
{
// The own licences in the order code, hardware, documentation.
juce::String joinedLicences (const juce::StringArray& ids)
{
    juce::StringArray shown;
    for (const auto* family : { "GPL-3.0", "AGPL-3.0", "CERN-OHL", "CC-BY-SA" })
        for (const auto& id : ids)
        {
            const auto text = shortLicence (id);
            if (text.startsWith (family) && ! shown.contains (text))
                shown.add (text);
        }
    return shown.joinIntoString (juce::String::fromUTF8 (" · "));
}

juce::String firstLicence (const juce::String& joined) { return joined.upToFirstOccurrenceOf (juce::String::fromUTF8 (" · "), false, false); }

// The paragraphs of a dep5 (Debian copyright format): each field's lines, its first line's value and then its continuation
// lines (indented; " ." an empty line), trimmed.
using Paragraph = std::map<juce::String, juce::StringArray>;

std::vector<Paragraph> dep5Paragraphs (const juce::File& file)
{
    std::vector<Paragraph> paragraphs (1);
    juce::String field;
    for (const auto& raw : juce::StringArray::fromLines (file.loadFileAsString()))
    {
        if (raw.trim().isEmpty())
        {
            if (! paragraphs.back().empty())
                paragraphs.emplace_back();
            field.clear();
            continue;
        }
        if ((raw.startsWithChar (' ') || raw.startsWithChar ('\t')) && field.isNotEmpty())
            paragraphs.back()[field].add (raw.trim() == "." ? juce::String() : raw.trim());
        else if (raw.containsChar (':') && ! raw.startsWithChar ('#'))
        {
            field = raw.upToFirstOccurrenceOf (":", false, false).trim();
            paragraphs.back()[field].add (raw.fromFirstOccurrenceOf (":", false, false).trim());
        }
    }
    if (paragraphs.back().empty())
        paragraphs.pop_back();
    return paragraphs;
}

juce::String firstLineOf (const Paragraph& paragraph, const char* field)
{
    const auto found = paragraph.find (field);
    return found != paragraph.end() && ! found->second.isEmpty() ? found->second[0] : juce::String();
}

juce::StringArray patternsOf (const Paragraph& paragraph)
{
    juce::StringArray patterns;
    if (const auto found = paragraph.find ("Files"); found != paragraph.end())
        for (const auto& line : found->second)
            patterns.addTokens (line, " \t", {});
    patterns.removeEmptyStrings();
    return patterns;
}

// "Files: a b" stanzas of a dep5: (patterns, license).
std::vector<std::pair<juce::StringArray, juce::String>> dep5Stanzas (const juce::File& file)
{
    std::vector<std::pair<juce::StringArray, juce::String>> stanzas;
    for (const auto& paragraph : dep5Paragraphs (file))
        if (paragraph.count ("Files") != 0 && paragraph.count ("License") != 0)
            stanzas.push_back ({ patternsOf (paragraph), firstLineOf (paragraph, "License") });
    return stanzas;
}

juce::StringArray idsOf (const juce::String& expression)
{
    return juce::StringArray::fromTokens (expression.replace (" AND ", " ").replace (" OR ", " ").replace ("(", " ").replace (")", " "), " ", {});
}

juce::String licenceFromHeaders (const juce::File& folder)
{
    std::map<juce::String, int> counts;
    int looked = 0;
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, true, "*.cpp;*.c;*.h;*.hpp;*.ino;*.py"))
    {
        if (++looked > 40)
            break;
        const auto head = file.loadFileAsString().substring (0, 2048);
        const auto id = head.fromFirstOccurrenceOf ("SPDX-License-Identifier:", false, false).upToFirstOccurrenceOf ("\n", false, false).trim();
        if (head.contains ("SPDX-License-Identifier:") && id.isNotEmpty())
            ++counts[id];
    }
    juce::String best;
    int most = 0;
    for (const auto& [id, count] : counts)
        if (count > most)
        {
            best = id;
            most = count;
        }
    return joinedLicences (idsOf (best));
}

// The version of a folder: the newest release up to `commit` when nothing in the folder changed since it, else that release
// and the number of commits since that touched the folder ("v03.1+2"). Empty when git knows no release or no such folder.
juce::String lastChangeTag (CommandRunner& runner, const juce::File& dir, const juce::String& commit, const juce::String& folder)
{
    const auto release = gitOutput (runner, dir, { "describe", "--tags", "--match", "v*", "--abbrev=0", commit });
    if (release.isEmpty() || gitOutput (runner, dir, { "log", "-1", "--format=%H", commit, "--", folder }).isEmpty())
        return {};
    const auto since = gitOutput (runner, dir, { "rev-list", "--count", release + ".." + commit, "--", folder });
    return since.isEmpty() || since == "0" ? release : release + "+" + since;
}

bool hasFirmware (const juce::String& submodule)
{
    return std::any_of (std::begin (firmwareSubmodules), std::end (firmwareSubmodules),
                        [&submodule] (const char* name) { return submodule == name; });
}

// Hardware and firmware rows read "rev <version>", lowercase whatever the file says; nothing when there is no version.
juce::String revisionText (const juce::String& version) { return version.isNotEmpty() ? "rev " + version.toLowerCase() : juce::String(); }

// An entry once by name: a second sighting only fills what the first did not know.
void addThirdParty (std::vector<ThirdParty>& others, const ThirdParty& entry)
{
    const auto found = std::find_if (others.begin(), others.end(), [&entry] (const ThirdParty& e) { return e.name == entry.name; });
    if (found == others.end())
    {
        others.push_back (entry);
        return;
    }
    const auto fill = [] (juce::String& mine, const juce::String& theirs) { if (mine.isEmpty()) mine = theirs; };
    fill (found->version, entry.version);
    fill (found->licence, entry.licence);
    fill (found->url, entry.url);
}

void addThirdParties (std::vector<ThirdParty>& others, const juce::File& repoDir)
{
    for (const auto& entry : thirdPartyOf (repoDir))
        addThirdParty (others, entry);
}

// A submodule published outside a3-audio: its describe (any tag, it is not ours), the short commit when not checked out.
void addForeignLink (CommandRunner& runner, const juce::File& dir, const Gitlink& link, const juce::String& url, std::vector<ThirdParty>& others)
{
    auto version = releaseText (gitOutput (runner, dir.getChildFile (link.path), { "describe", "--tags", "--always", link.commit }));
    if (version.isEmpty())
        version = link.commit.substring (0, 7);
    addThirdParty (others, { lastComponent (link.path), version, {}, url });
}

// The submodules of a checked-out repository, anywhere in its tree, each a row "<parent>/<last path component>", and
// theirs below them -- any depth (a3-stemdeck's a3-stem-separator, a3-mixer's hardware/a3-kicad-lib).
// A part of a repository (pcb, housing, firmware, ui folder) by its folder: a submodule inside it is that part's child.
struct PartFolder
{
    juce::String folder, rowName;
};

juce::String parentRowOf (const juce::String& path, const juce::String& repoRow, const std::vector<PartFolder>& partFolders)
{
    for (const auto& part : partFolders)
        if (path.startsWith (part.folder + "/"))
            return part.rowName;
    return repoRow;
}

// A repository the system already lists (one of the umbrella's, or a row above): an embedded copy of it is not a second
// part (a3-stemdeck's third_party/a3-core is the system's a3-core).
bool isListed (const juce::String& url, const std::set<juce::String>& systemUrls, const Facts& facts)
{
    if (url.isEmpty() || url.startsWith ("../"))
        return false;
    return systemUrls.count (url) != 0
        || std::any_of (facts.rows.begin(), facts.rows.end(), [&url] (const Row& row) { return row.url == url; });
}

void addNested (CommandRunner& runner, const juce::File& dir, const juce::String& commit, const std::vector<Gitlink>& inners,
                const juce::String& url, const juce::String& parentName, Facts& facts, std::vector<ThirdParty>& others,
                const std::set<juce::String>& systemUrls, const std::vector<PartFolder>& partFolders = {})
{
    if (inners.empty())
        return;
    const auto innerUrls = urlsAt (runner, dir, commit, url);
    for (const auto& inner : inners)
    {
        const auto innerDir = dir.getChildFile (inner.path);
        const auto name = parentRowOf (inner.path, parentName, partFolders) + "/" + lastComponent (inner.path);
        const auto innerUrl = urlOf (innerUrls, inner.path);
        if (! isOurs (innerUrl))
        {
            addForeignLink (runner, dir, inner, innerUrl, others);
            continue;
        }
        if (isListed (innerUrl, systemUrls, facts))
            continue;
        facts.rows.push_back ({ name, tagOf (runner, innerDir, inner.commit), innerUrl, codeLicence (innerDir) });
        if (facts.rows.back().tag.isEmpty())
            continue;
        addThirdParties (others, innerDir);
        addNested (runner, innerDir, inner.commit, parseGitlinks (gitOutput (runner, innerDir, { "ls-tree", "-r", inner.commit })),
                   innerUrl, name, facts, others, systemUrls);
    }
}

// A submodule's row, then its parts (pcb, housing), its firmware's, its ui folder's, then its own submodules at any depth.
void addSubmodule (CommandRunner& runner, const juce::File& repo, const Gitlink& link, const juce::String& url, Facts& facts,
                   std::vector<ThirdParty>& others, const std::set<juce::String>& systemUrls)
{
    if (! isOurs (url))
    {
        addForeignLink (runner, repo, link, url, others);
        return;
    }
    const auto dir = repo.getChildFile (link.path);
    const auto rowName = lastComponent (link.path);
    facts.rows.push_back ({ rowName, tagOf (runner, dir, link.commit), url, codeLicence (dir) });
    if (facts.rows.back().tag.isEmpty())
        return;   // not checked out: nothing more to ask it
    addThirdParties (others, dir);
    std::vector<PartFolder> partFolders;
    // A hardware part has the revision its REVISION file says.
    for (const auto& part : partsOf (dir))
    {
        partFolders.push_back ({ part.folder, rowName + " " + part.name });
        const auto version = revisionText (part.revision);
        facts.rows.push_back ({ rowName + " " + part.name, version, url.isNotEmpty() ? url + "/tree/HEAD/" + part.folder : juce::String(),
                                licenceOfFolder (dir, part.folder), version.isEmpty() });
    }
    if (hasFirmware (link.path))
    {
        partFolders.push_back ({ firmwareFolder, rowName + " firmware" });
        facts.rows.push_back ({ rowName + " firmware", revisionText (lastChangeTag (runner, dir, link.commit, firmwareFolder)),
                                url.isNotEmpty() ? url + "/tree/HEAD/" + firmwareFolder : juce::String(),
                                licenceOfFolder (dir, firmwareFolder) });
    }
    const auto inners = parseGitlinks (gitOutput (runner, dir, { "ls-tree", "-r", link.commit }));
    // A ui that is a folder of the repo (a3-mixer's desk) is a part like the firmware, with its software version;
    // a ui that is a repository of its own (a3-motion's) is listed below as a submodule.
    const bool uiIsSubmodule = std::any_of (inners.begin(), inners.end(), [] (const Gitlink& g) { return g.path == uiFolder; });
    if (! uiIsSubmodule && dir.getChildFile (uiFolder).isDirectory())
    {
        partFolders.push_back ({ uiFolder, rowName + " ui" });
        facts.rows.push_back ({ rowName + " ui", lastChangeTag (runner, dir, link.commit, uiFolder),
                                url.isNotEmpty() ? url + "/tree/HEAD/" + uiFolder : juce::String(),
                                licenceOfFolder (dir, uiFolder) });
    }
    addNested (runner, dir, link.commit, inners, url, rowName, facts, others, systemUrls, partFolders);
}

// The JUCE this program is compiled against, from JUCE's own header at build time.
juce::String builtJuceVersion()
{
    return juce::String (JUCE_MAJOR_VERSION) + "." + juce::String (JUCE_MINOR_VERSION) + "." + juce::String (JUCE_BUILDNUMBER);
}

// The tree's last node: the third parties sorted by name, JUCE with the version the program is built with.
void addThirdPartyNode (std::vector<ThirdParty> others, Facts& facts)
{
    if (others.empty())
        return;
    std::sort (others.begin(), others.end(), [] (const ThirdParty& a, const ThirdParty& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    facts.rows.push_back ({ thirdPartyNode, {}, {}, {}, true });
    for (auto& entry : others)
    {
        if (entry.name == "JUCE" && entry.version.isEmpty())
            entry.version = builtJuceVersion();
        facts.rows.push_back ({ juce::String (thirdPartyNode) + "/" + entry.name, entry.version, entry.url, entry.licence, entry.version.isEmpty() });
    }
}
}

std::map<juce::String, juce::String> parseUrls (const juce::String& output, const juce::String& base)
{
    std::map<juce::String, juce::String> urls;
    for (const auto& line : juce::StringArray::fromLines (output))
    {
        const auto key = line.upToFirstOccurrenceOf (" ", false, false).trim();
        auto url = line.fromFirstOccurrenceOf (" ", false, false).trim();
        if (! key.startsWith ("submodule.") || ! key.endsWith (".url") || url.isEmpty())
            continue;
        const auto name = key.fromFirstOccurrenceOf ("submodule.", false, false).upToLastOccurrenceOf (".url", false, false);
        while (url.startsWith ("../") && base.isNotEmpty())
            url = base.upToLastOccurrenceOf ("/", false, false) + "/" + url.substring (3);
        urls[name] = url.endsWith (".git") ? url.dropLastCharacters (4) : url;
    }
    return urls;
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

juce::String codeLicence (const juce::File& repoDir)
{
    juce::StringArray ids;
    for (const auto& file : repoDir.getChildFile ("LICENSES").findChildFiles (juce::File::findFiles, false, "*.txt"))
        ids.add (file.getFileNameWithoutExtension());
    ids.sort (false);
    return firstLicence (joinedLicences (ids));
}

namespace
{
juce::String revisionIn (const juce::File& folder)
{
    return folder.getChildFile ("REVISION").loadFileAsString().upToFirstOccurrenceOf ("\n", false, false).trim();
}

bool holdsFile (const juce::File& folder, std::initializer_list<const char*> patterns)
{
    for (const auto* pattern : patterns)
        if (! folder.findChildFiles (juce::File::findFiles, true, pattern).isEmpty())
            return true;
    return false;
}
}

namespace
{
// A revision folder of hardware/pcb: a folder with a KiCad project of its own or a REVISION file. The shared library and
// a mounted a3-kicad-lib hold neither, so they are no revision.
bool isRevisionFolder (const juce::File& folder)
{
    return folder.getChildFile ("REVISION").existsAsFile() || ! folder.findChildFiles (juce::File::findFiles, false, "*.kicad_pro").isEmpty();
}

std::vector<juce::File> revisionFoldersOf (const juce::File& pcbRoot)
{
    std::vector<juce::File> folders;
    for (const auto& folder : pcbRoot.findChildFiles (juce::File::findDirectories, false))
        if (isRevisionFolder (folder))
            folders.push_back (folder);
    std::sort (folders.begin(), folders.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
    return folders;
}

// The board is the shallowest project (a sub-project such as enc_pot sits below it); ties go to the first path,
// whatever order the directory is read in.
juce::File shallowestProjectFolder (const juce::File& hardware)
{
    auto projects = hardware.findChildFiles (juce::File::findFiles, true, "*.kicad_pro");
    if (projects.isEmpty())
        return {};
    const auto depthOf = [&hardware] (const juce::File& f) { return f.getRelativePathFrom (hardware).replaceCharacter ('\\', '/').retainCharacters ("/").length(); };
    std::sort (projects.begin(), projects.end(), [&] (const juce::File& a, const juce::File& b) {
        const auto depthA = depthOf (a), depthB = depthOf (b);
        return depthA != depthB ? depthA < depthB : a.getFullPathName() < b.getFullPathName();
    });
    return projects.getFirst().getParentDirectory();
}
}

std::vector<Part> partsOf (const juce::File& repoDir)
{
    std::vector<Part> parts;
    const auto relative = [&repoDir] (const juce::File& f) { return f.getRelativePathFrom (repoDir); };
    const auto hardware = repoDir.getChildFile ("hardware");
    // Revision folders under hardware/pcb: pcb is a node and each folder a child row. Without them, one row for the board.
    const auto pcbRoot = hardware.getChildFile ("pcb");
    const auto revisions = revisionFoldersOf (pcbRoot);
    juce::File pcbFolder;
    if (! revisions.empty())
    {
        pcbFolder = pcbRoot;
        parts.push_back ({ "pcb", relative (pcbRoot), {} });
        for (const auto& folder : revisions)
            parts.push_back ({ "pcb/" + folder.getFileName(), relative (folder), revisionIn (folder) });
    }
    else if (const auto boardFolder = shallowestProjectFolder (hardware); boardFolder != juce::File())
    {
        pcbFolder = boardFolder;
        parts.push_back ({ "pcb", relative (pcbFolder), revisionIn (pcbFolder) });
    }
    for (const auto& top : hardware.findChildFiles (juce::File::findDirectories, false))
    {
        // The board's own folder or the one holding it: a library mounted in it (a3-kicad-lib's 3D models) makes no housing.
        const bool isPcbsFolder = pcbFolder != juce::File() && (pcbFolder == top || pcbFolder.isAChildOf (top));
        if (isPcbsFolder || ! holdsFile (top, { "*.FCStd", "*.stl", "*.3mf", "*.blend" }))
            continue;
        parts.push_back ({ "housing", relative (top), revisionIn (top) });
        break;
    }
    return parts;
}

juce::String licenceOfFolder (const juce::File& repoDir, const juce::String& folder)
{
    const auto probe = folder + "/src/main.cpp";
    juce::String covered;
    for (const auto& [patterns, license] : dep5Stanzas (repoDir.getChildFile (".reuse/dep5")))
        for (const auto& pattern : patterns)
        {
            if (pattern.endsWith (".md") || pattern.endsWith (".ini") || ! probe.matchesWildcard (pattern, false))
                continue;
            covered = license;
        }
    const auto fromDep5 = joinedLicences (idsOf (covered));
    return firstLicence (fromDep5.isNotEmpty() ? fromDep5 : licenceFromHeaders (repoDir.getChildFile (folder)));
}

bool isOurs (const juce::String& url)
{
    if (url.isEmpty() || url.startsWith ("../") || url.startsWith ("./"))
        return true;
    for (const auto* home : { "https://github.com/a3-audio/", "http://github.com/a3-audio/", "git@github.com:a3-audio/", "ssh://git@github.com/a3-audio/" })
        if (url.startsWith (home))
            return true;
    return false;
}

juce::String thirdPartyLicence (const juce::String& expression)
{
    juce::StringArray words;
    for (auto word : juce::StringArray::fromTokens (expression, " ", {}))
    {
        word = word.replace ("LicenseRef-", "");
        for (const auto* suffix : { "-or-later", "-only" })
            if (word.endsWith (suffix))
                word = word.dropLastCharacters ((int) std::strlen (suffix));
        words.add (word);
    }
    return words.joinIntoString (" ");
}

namespace
{
// A holder of ours: the copyright holder of 2026-10-30, the project, and Patric Schmitz, a member co-holding the mixer firmware.
bool isOurHolder (const juce::String& holder)
{
    for (const auto* ours : { "Raphael Eismann", "Raphael Eismann and contributors", "Patric Schmitz", "A3 Audio", "A3 Audio contributors",
                              "a3-audio contributors" })
        if (holder.equalsIgnoreCase (ours))
            return true;
    return false;
}

// The holders of a Copyright field: each line without its years and e-mail address, split at commas.
juce::StringArray holdersOf (const juce::StringArray& lines)
{
    juce::StringArray holders;
    for (auto line : lines)
    {
        line = (line.upToFirstOccurrenceOf ("<", false, false) + line.fromFirstOccurrenceOf (">", false, false)).trim();
        while (line.isNotEmpty() && (juce::String ("0123456789-, ").containsChar (line[0]) || line.startsWith ("present")))
            line = line.startsWith ("present") ? line.substring (7) : line.substring (1);
        holders.addTokens (line, ",", {});
    }
    holders.trim();
    holders.removeEmptyStrings();
    return holders;
}

bool isForeign (const Paragraph& stanza)
{
    const auto found = stanza.find ("Copyright");
    if (found == stanza.end())
        return false;
    const auto holders = holdersOf (found->second);
    return std::any_of (holders.begin(), holders.end(), [] (const juce::String& h) { return ! isOurHolder (h); });
}

bool coversTrivialFiles (const Paragraph& stanza)
{
    const auto ids = idsOf (firstLineOf (stanza, "License"));
    return std::all_of (ids.begin(), ids.end(), [] (const juce::String& id) { return id == "CC0-1.0"; });
}

juce::String nameOf (const Paragraph& stanza)
{
    if (const auto name = firstLineOf (stanza, "X-A3-Name"); name.isNotEmpty())
        return name;
    if (const auto found = stanza.find ("Comment"); found != stanza.end())
    {
        const auto comment = found->second.joinIntoString (" ").trim();
        const auto sentence = comment.upToFirstOccurrenceOf (". ", false, false).trim();
        if (sentence.isNotEmpty())
            return sentence.endsWithChar ('.') ? sentence.dropLastCharacters (1) : sentence;
    }
    return patternsOf (stanza)[0];
}

// "JUCE; AGPL-3.0-only OR LicenseRef-JUCE-9; https://juce.com" -> its entry.
ThirdParty usedFrom (const juce::String& line)
{
    auto fields = juce::StringArray::fromTokens (line, ";", {});
    fields.trim();
    return { fields[0], {}, thirdPartyLicence (fields[1]), fields[2] };
}
}

std::vector<ThirdParty> thirdPartyOf (const juce::File& repoDir)
{
    std::vector<ThirdParty> entries;
    const auto paragraphs = dep5Paragraphs (repoDir.getChildFile (".reuse/dep5"));
    for (const auto& stanza : paragraphs)
    {
        if (stanza.count ("Files") == 0 || ! isForeign (stanza) || coversTrivialFiles (stanza))
            continue;
        const auto own = firstLineOf (stanza, "X-A3-License");
        entries.push_back ({ nameOf (stanza), firstLineOf (stanza, "X-A3-Version"), thirdPartyLicence (own.isNotEmpty() ? own : firstLineOf (stanza, "License")),
                             firstLineOf (stanza, "X-A3-Url") });
    }
    if (! paragraphs.empty() && paragraphs.front().count ("Files") == 0)
        if (const auto uses = paragraphs.front().find ("X-A3-Uses"); uses != paragraphs.front().end())
            for (const auto& line : uses->second)
                if (line.isNotEmpty())
                    entries.push_back (usedFrom (line));
    return entries;
}

juce::String tagText (const Row& row) { return row.tag.isNotEmpty() ? row.tag : row.noVersion ? juce::String() : juce::String ("not checked out"); }

Facts read (CommandRunner& runner, const juce::File& repo)
{
    Facts facts;
    const auto links = parseGitlinks (gitOutput (runner, repo, { "ls-tree", "-r", "HEAD" }));
    if (links.empty())
        return facts;
    facts.found = true;
    facts.licence = codeLicence (repo);
    facts.version = releaseText (gitOutput (runner, repo, { "describe", "--tags", "--match", "v*", "--always", "HEAD" }));
    const auto urls = urlsAt (runner, repo, "HEAD", {});
    std::vector<ThirdParty> others;
    addThirdParties (others, repo);
    std::set<juce::String> systemUrls;
    for (const auto& [path, url] : urls)
        systemUrls.insert (url);
    // Every submodule of ours is a part of the release since 2026-10-31 (stemseparator too, as stemdeck's).
    for (const auto& link : links)
        addSubmodule (runner, repo, link, urlOf (urls, link.path), facts, others, systemUrls);
    addThirdPartyNode (std::move (others), facts);
    return facts;
}

namespace
{
juce::String parentOf (const juce::String& name)
{
    if (name.contains ("/"))
        return name.upToLastOccurrenceOf ("/", false, false);
    return name.contains (" ") ? name.upToFirstOccurrenceOf (" ", false, false) : juce::String();
}

void addChildren (const std::vector<Row>& rows, size_t parent, const juce::String& indent, std::vector<TreeEntry>& tree)
{
    const juce::String branch = juce::String::fromUTF8 ("├─ "), corner = juce::String::fromUTF8 ("└─ "),
                       bar = juce::String::fromUTF8 ("│  "), blank = "   ";
    std::vector<size_t> children;
    for (size_t i = 0; i < rows.size(); ++i)
        if (i != parent && parentOf (rows[i].name) == rows[parent].name)
            children.push_back (i);
    for (size_t c = 0; c < children.size(); ++c)
    {
        const bool last = c + 1 == children.size();
        tree.push_back ({ children[c], rows[children[c]].name.substring (rows[parent].name.length() + 1), indent + (last ? corner : branch) });
        addChildren (rows, children[c], indent + (last ? blank : bar), tree);
    }
}
}

std::vector<TreeEntry> treeOf (const std::vector<Row>& rows)
{
    std::vector<size_t> tops;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto parent = parentOf (rows[i].name);
        if (parent.isEmpty() || std::none_of (rows.begin(), rows.end(), [&] (const Row& r) { return r.name == parent; }))
            tops.push_back (i);
    }
    std::vector<TreeEntry> tree;
    for (size_t k = 0; k < tops.size(); ++k)
    {
        const bool last = k + 1 == tops.size();
        tree.push_back ({ tops[k], rows[tops[k]].name, juce::String::fromUTF8 (last ? "└─ " : "├─ ") });
        addChildren (rows, tops[k], last ? "   " : juce::String::fromUTF8 ("│  "), tree);
    }
    return tree;
}
}
