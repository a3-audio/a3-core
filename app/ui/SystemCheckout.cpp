// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemCheckout.h"
#include <algorithm>

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

std::map<juce::String, juce::String> urlsAt (CommandRunner& runner, const juce::File& dir, const juce::String& commit, const juce::String& base)
{
    return parseUrls (gitOutput (runner, dir, { "config", "--blob", commit + ":.gitmodules", "--get-regexp", "^submodule\\..*\\.url$" }), base);
}

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

// "Files: a b" stanzas of a dep5: (patterns, license).
std::vector<std::pair<juce::StringArray, juce::String>> dep5Stanzas (const juce::File& file)
{
    std::vector<std::pair<juce::StringArray, juce::String>> stanzas;
    juce::StringArray patterns;
    bool inFiles = false;
    for (const auto& raw : juce::StringArray::fromLines (file.loadFileAsString()))
    {
        if (raw.trim().isEmpty())
        {
            inFiles = false;
            continue;
        }
        if (raw.startsWith ("Files:"))
        {
            patterns = juce::StringArray::fromTokens (raw.fromFirstOccurrenceOf (":", false, false), " \t", {});
            inFiles = true;
        }
        else if (inFiles && raw.startsWithChar (' '))
            patterns.addTokens (raw, " \t", {});
        else if (raw.startsWith ("License:"))
        {
            inFiles = false;
            stanzas.push_back ({ patterns, raw.fromFirstOccurrenceOf (":", false, false).trim() });
        }
        else
            inFiles = false;
    }
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

const FirmwareDir* firmwareOf (const juce::String& submodule)
{
    for (const auto& dir : firmwareDirs)
        if (submodule == dir.submodule)
            return &dir;
    return nullptr;
}

// Hardware and firmware rows read "rev <version>", lowercase whatever the file says; nothing when there is no version.
juce::String revisionText (const juce::String& version) { return version.isNotEmpty() ? "rev " + version.toLowerCase() : juce::String(); }

// A submodule's row, then its parts (docs, pcb, housing), its firmware's, then its own submodules' (one level: a3-motion's ui).
void addSubmodule (CommandRunner& runner, const juce::File& repo, const Gitlink& link, const juce::String& url, Facts& facts)
{
    const auto dir = repo.getChildFile (link.path);
    facts.rows.push_back ({ link.path, tagOf (runner, dir, link.commit), url, codeLicence (dir) });
    if (facts.rows.back().tag.isEmpty())
        return;   // not checked out: nothing more to ask it
    // A hardware part has the revision its REVISION file says.
    for (const auto& part : partsOf (dir))
    {
        const auto version = revisionText (part.revision);
        facts.rows.push_back ({ link.path + " " + part.name, version, url.isNotEmpty() ? url + "/tree/HEAD/" + part.folder : juce::String(),
                                licenceOfFolder (dir, part.folder), version.isEmpty() });
    }
    if (const auto* firmware = firmwareOf (link.path))
    {
        facts.rows.push_back ({ link.path + " firmware", revisionText (lastChangeTag (runner, dir, link.commit, firmware->path)),
                                url.isNotEmpty() ? url + "/tree/HEAD/" + firmware->path : juce::String(),
                                licenceOfFolder (dir, firmware->path) });
    }
    const auto innerUrls = urlsAt (runner, dir, link.commit, url);
    for (const auto& inner : parseGitlinks (gitOutput (runner, dir, { "ls-tree", link.commit })))
        facts.rows.push_back ({ link.path + "/" + inner.path, tagOf (runner, dir.getChildFile (inner.path), inner.commit), urlOf (innerUrls, inner.path),
                                codeLicence (dir.getChildFile (inner.path)) });
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

std::vector<Part> partsOf (const juce::File& repoDir)
{
    std::vector<Part> parts;
    const auto relative = [&repoDir] (const juce::File& f) { return f.getRelativePathFrom (repoDir); };
    const auto hardware = repoDir.getChildFile ("hardware");
    juce::File pcbFolder;
    auto projects = hardware.findChildFiles (juce::File::findFiles, true, "*.kicad_pro");
    // The board is the shallowest project (a sub-project such as enc_pot sits below it); ties go to the first path,
    // whatever order the directory is read in.
    const auto depthOf = [&hardware] (const juce::File& f) { return f.getRelativePathFrom (hardware).replaceCharacter ('\\', '/').retainCharacters ("/").length(); };
    std::sort (projects.begin(), projects.end(), [&] (const juce::File& a, const juce::File& b) {
        const auto depthA = depthOf (a), depthB = depthOf (b);
        return depthA != depthB ? depthA < depthB : a.getFullPathName() < b.getFullPathName();
    });
    if (! projects.isEmpty())
    {
        pcbFolder = projects.getFirst().getParentDirectory();
        parts.push_back ({ "pcb", relative (pcbFolder), revisionIn (pcbFolder) });
    }
    for (const auto& top : hardware.findChildFiles (juce::File::findDirectories, false))
    {
        const bool isPcbsFolder = pcbFolder != juce::File() && pcbFolder.isAChildOf (top);
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

bool isInRelease (const juce::String& submodulePath) { return submodulePath != "stemseparator"; }

juce::String tagText (const Row& row) { return row.tag.isNotEmpty() ? row.tag : row.noVersion ? juce::String() : juce::String ("not checked out"); }

Facts read (CommandRunner& runner, const juce::File& repo)
{
    Facts facts;
    const auto links = parseGitlinks (gitOutput (runner, repo, { "ls-tree", "HEAD" }));
    if (links.empty())
        return facts;
    facts.found = true;
    facts.licence = codeLicence (repo);
    facts.version = releaseText (gitOutput (runner, repo, { "describe", "--tags", "--match", "v*", "--always", "HEAD" }));
    const auto urls = urlsAt (runner, repo, "HEAD", {});
    for (const auto& link : links)
        if (isInRelease (link.path))
            addSubmodule (runner, repo, link, urlOf (urls, link.path), facts);
    return facts;
}
}
