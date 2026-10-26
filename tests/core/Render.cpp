// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_osc_render.py (B0 f3345dc7feeb): what lives outside the core
// is rendered from the one truth. Plus what tools/parity/record_render.py recorded from B0's
// renderer -- the strings and four homes, compared byte for byte. The tests that read the package's
// own files (postinst, templates, the x11vnc unit, the viewer recipe) read B0's copies from
// data/parity/render/package/; Task 27 rewrites the live files and their pins.
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

#include <gtest/gtest.h>

#include "B0Truth.h"
#include "Render.h"
#include "StateRecorded.h"

using namespace a3core;

namespace
{
const juce::File renderData (A3_CORE_TEST_DATA "/parity/render");

std::string fixture (const juce::String& relative) { return recorded::readBytes (renderData.getChildFile (relative)); }

juce::File aes67Template() { return renderData.getChildFile ("aes67.conf.in"); }

// KEY=value lines, comments and blanks left out.
std::map<std::string, std::string> pairs (const std::string& text)
{
    std::map<std::string, std::string> found;
    std::istringstream stream (text);
    for (std::string line; std::getline (stream, line);)
    {
        const auto trimmed = juce::String (line).trim().toStdString();
        const auto equals = trimmed.find ('=');
        if (trimmed.empty() || trimmed[0] == '#' || equals == std::string::npos)
            continue;
        found[trimmed.substr (0, equals)] = trimmed.substr (equals + 1);
    }
    return found;
}

std::vector<std::string> linesOf (const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream stream (text);
    for (std::string line; std::getline (stream, line);)
        out.push_back (line);
    return out;
}

bool someLineMatches (const std::string& text, const std::string& pattern)
{
    const std::regex re (pattern);
    for (const auto& line : linesOf (text))
        if (std::regex_search (line, re))
            return true;
    return false;
}

juce::var parsed (const std::string& text)
{
    juce::String error;
    auto value = parseJson (juce::String::fromUTF8 (text.data(), static_cast<int> (text.size())), error);
    EXPECT_EQ (error, juce::String());
    return value;
}

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> members)
{
    auto* made = new juce::DynamicObject();
    for (const auto& member : members)
        made->setProperty (juce::Identifier (member.first), member.second);
    return juce::var (made);
}

// The contract with a network.json joined in, as Truth::load makes of it.
Truth joinedWith (const std::string& key, const juce::var& value)
{
    const auto contract = b0::truth().data();
    auto network = contract["network"].clone();
    network.getDynamicObject()->setProperty (juce::Identifier (key), value);
    auto local = object ({ { "hosts", contract["hosts"].clone() }, { "network", network } });
    return Truth (joinTruth (contract, local));
}

Truth shipped() { return b0::truth(); }

// The shipped file as it is on disk, keys in file order -- the canonical text b0::truth() is built
// from sorts them, and json.dumps without sort_keys writes the order it was given.
Truth shippedFile()
{
    auto loaded = Truth::load (renderData.getChildFile ("package/a3-osc.json"), std::nullopt);
    EXPECT_TRUE (std::holds_alternative<Truth> (loaded));
    return std::get<Truth> (loaded);
}
Truth withoutAudioPort() { return joinedWith ("audio_port", juce::String()); }

juce::File truthFile (recorded::TempDir& temp)
{
    const auto file = temp.dir.getChildFile ("a3-osc.json");
    recorded::writeBytes (file, b0::truth().canonical());
    return file;
}

constexpr const char* hostname = "a3nuc1";

std::string analyzerOf (const juce::File& home) { return recorded::readBytes (home.getChildFile (analyzerConfPath)); }
}

// ---- zita is gone (2026-10-14) ----

TEST (Render, TheRenderedFileNamesNoZita) { EXPECT_EQ (oscEnv (shipped()).find ("ZITA"), std::string::npos); }

// ---- VNC ----

TEST (Render, TheTruthNamesVncsListener) { EXPECT_EQ (shipped().listener ("x11vnc", "vnc")["host"].toString(), "any"); }

TEST (Render, TheRenderedFileCarriesVncsPort)
{
    EXPECT_EQ (std::stoi (pairs (oscEnv (shipped()))["A3_VNC_PORT"]), shipped().port ("x11vnc", "vnc"));
}

TEST (Render, TheUnitTakesItsPortFromTheRenderedFile)
{
    const auto text = fixture ("package/x11vnc.service");
    // A system unit's %h is root's home, whatever User= says.
    EXPECT_NE (text.find (std::string ("EnvironmentFile=/home/aaa/") + envFilePath), std::string::npos);
    std::string start;
    for (const auto& line : linesOf (text))
        if (line.rfind ("ExecStart=", 0) == 0)
            start = line;
    EXPECT_NE (start.find ("-rfbport ${A3_VNC_PORT}"), std::string::npos);
    EXPECT_FALSE (std::regex_search (start, std::regex (R"(\b\d{4,5}\b)")));
}

TEST (Render, TheViewerRecipeAsksTheTruth)
{
    // The recipe is a script that asks the truth for the Core's host and VNC's port; run, it prints
    // "QualityLevel 2 <host>::<port>". The script is B0's copy and needs Python to run, which this
    // suite does not (O12): its question is held against the truth, and so is the line it builds.
    const auto recipe = fixture ("package/a3vnc.sh");
    EXPECT_NE (recipe.find ("print(truth.host(\"core\"), truth.port(\"x11vnc\", \"vnc\"))"), std::string::npos);
    EXPECT_NE (recipe.find ("vncviewer QualityLevel 2 \"${host}::${port}\""), std::string::npos);
    EXPECT_EQ (fixture ("texts/viewer-recipe-output"),
               "QualityLevel 2 " + shipped().host ("core") + "::" + std::to_string (shipped().port ("x11vnc", "vnc")) + "\n");
}

// ---- the default network ----

TEST (Render, TheDefaultNetworkIsTheTruths)
{
    const auto shell = pairs (networkDefaults (shipped()));
    const auto network = shipped().network();
    EXPECT_EQ (shell.at ("INTERFACE"), network["interface"].toString().toStdString());
    EXPECT_EQ (shell.at ("ADDR"), network["address"].toString().toStdString());
    EXPECT_EQ (shell.at ("GATEWAY"), network["gateway"].toString().toStdString());
    EXPECT_EQ (shell.at ("DNS"), network["dns"].toString().toStdString());
}

TEST (Render, TheDefaultPutsTheAudioNetworkOnTheOtherPort)
{
    const auto shell = pairs (networkDefaults (shipped()));
    const auto network = shipped().network();
    EXPECT_EQ (shell.at ("AUDIO_PORT"), network["audio_port"].toString().toStdString());
    EXPECT_EQ (shell.at ("AUDIO_ADDR"), network["audio_address"].toString().toStdString());
    EXPECT_EQ (shell.count ("BRIDGE_WITH"), 0u);
}

TEST (Render, PostinstTakesTheDefaultFromTheTool)
{
    const auto text = fixture ("package/postinst");
    EXPECT_TRUE (someLineMatches (text, R"(a3-osc-render"? network)"));
    EXPECT_EQ (text.find ("ADDR=\"192.168."), std::string::npos);
}

TEST (Render, TheQuestionsDefaultToTheTruth)
{
    // debconf reads its defaults from the static templates file, so they cannot be rendered -- they
    // are held against the truth instead.
    std::map<std::string, std::string> defaults;
    std::string question;
    for (const auto& line : linesOf (fixture ("package/templates")))
    {
        if (line.rfind ("Template: ", 0) == 0)
            question = line.substr (10);
        else if (line.rfind ("Default:", 0) == 0)
            defaults[question] = juce::String (line.substr (8)).trim().toStdString();
    }
    const auto network = shipped().network();
    EXPECT_EQ (defaults["a3-core/interface"], network["interface"].toString().toStdString());
    EXPECT_EQ (defaults["a3-core/address"], network["address"].toString().toStdString());
    EXPECT_EQ (defaults["a3-core/gateway"], network["gateway"].toString().toStdString());
    EXPECT_EQ (defaults["a3-core/dns"], network["dns"].toString().toStdString());
    EXPECT_EQ (defaults["a3-core/audio-address"], network["audio_address"].toString().toStdString());
}

TEST (Render, NetworkDefaultsAsPython)
{
    EXPECT_EQ (networkDefaults (shipped()), fixture ("texts/network-defaults"));
    EXPECT_EQ (networkDefaults (joinedWith ("address", juce::String ("10.1.2.3/24"))), fixture ("texts/network-defaults-moved"));
}

// ---- the analyzer's targets ----

TEST (Render, EveryRouteFromTheAnalyzerIsATarget)
{
    const auto truth = shipped();
    const auto targets = pairs (analyzerBlock (truth));
    for (const auto& route : truth.routes())
    {
        if (route["from"].toString() != "beat-analyzer")
            continue;
        const auto to = route["to"].toString().toStdString();
        const auto dot = to.find ('.');
        const auto program = to.substr (0, dot), role = to.substr (dot + 1);
        const auto vu = role == "vu" || route["carries"].toString() == "vu";
        const auto endpoint = truth.endpoint (program, role);
        SCOPED_TRACE (to);
        EXPECT_EQ (targets.at ((vu ? "OSC_VU_" : "OSC_HOST_") + program), endpoint.host + ":" + std::to_string (endpoint.port));
    }
}

TEST (Render, TheMixerGetsItsMeters)
{
    // Once one OSC_VU_ target exists the analyzer sends /vu to those only -- a program on one port
    // for both needs its OSC_VU_ line too, or its meters stop (the mixer, whose meters sit on its
    // OSC port).
    const auto targets = pairs (analyzerBlock (shipped()));
    const auto endpoint = shipped().endpoint ("mixer", "osc");
    EXPECT_EQ (targets.at ("OSC_VU_mixer"), endpoint.host + ":" + std::to_string (endpoint.port));
}

TEST (Render, NothingElseIsATarget)
{
    const auto targets = pairs (analyzerBlock (shipped()));
    size_t routed = 0, sentTo = 0;
    for (const auto& route : shipped().routes())
        routed += route["from"].toString() == "beat-analyzer" ? 1 : 0;
    for (const auto& target : targets)
        sentTo += target.first.rfind ("OSC_HOST_", 0) == 0 || target.first.rfind ("OSC_VU_", 0) == 0 ? 1 : 0;
    EXPECT_EQ (sentTo, routed);
}

TEST (Render, TheAnalyzerSpeaksTheTruthsWords)
{
    // Its addresses and the Pro DJ Link ports, too (beat-analyzer's Config::OscWords reads them).
    const auto truth = shipped();
    const auto targets = pairs (analyzerBlock (truth));
    EXPECT_EQ (targets.at ("OSC_ADDRESS_BEAT"), truth.pattern ("beat"));
    EXPECT_EQ (targets.at ("OSC_ADDRESS_TAP"), truth.pattern ("tap"));
    EXPECT_EQ (targets.at ("OSC_ADDRESS_CLOCKMODE"), truth.pattern ("clockmode"));
    EXPECT_EQ (targets.at ("OSC_ADDRESS_VU"), truth.pattern ("vu"));
    EXPECT_EQ (std::stoi (targets.at ("PIONEER_PORT_ANNOUNCE")), truth.port ("prolink", "announce"));
    EXPECT_EQ (std::stoi (targets.at ("PIONEER_PORT_BEAT")), truth.port ("prolink", "beat"));
    EXPECT_EQ (std::stoi (targets.at ("PIONEER_PORT_STATUS")), truth.port ("prolink", "status"));
}

TEST (Render, TheAnalyzerListensWhereTheTruthSays)
{
    EXPECT_EQ (std::stoi (pairs (analyzerBlock (shipped())).at ("OSC_PORT_A3MOTION")), shipped().port ("beat-analyzer", "clock"));
}

// ---- the analyzer's file of its own ----

TEST (Render, TheFileSitsInTheAnalyzersConfD) { EXPECT_STREQ (analyzerConfPath, ".config/beat-analyzer/conf.d/50-a3-osc.env"); }

TEST (Render, TheFileSaysWhoseItIs)
{
    const auto block = analyzerBlock (shipped());
    const auto first = linesOf (block).front();
    EXPECT_EQ (first[0], '#');
    EXPECT_NE (first.find ("a3-osc-render"), std::string::npos);
}

TEST (Render, WrittenWholeAndTheSameAgain)
{
    recorded::TempDir temp;
    writeUserFiles (shipped(), temp.dir, hostname, aes67Template());
    const auto once = analyzerOf (temp.dir);
    EXPECT_EQ (once, analyzerBlock (shipped()));
    auto stale = once;
    stale.replace (stale.find ('='), 1, "=stale");
    recorded::writeBytes (temp.dir.getChildFile (analyzerConfPath), stale);
    writeUserFiles (shipped(), temp.dir, hostname, aes67Template());
    EXPECT_EQ (analyzerOf (temp.dir), once);
}

TEST (Render, AReaderNeverSeesAnEmptyFile)
{
    // Core rewrites the file at every start while the analyzer may be reading it: the new file is
    // swapped in whole, never emptied first.
    recorded::TempDir temp;
    const auto conf = temp.dir.getChildFile (analyzerConfPath);
    recorded::writeBytes (conf, "OLD=1\n");
    std::ifstream reader (conf.getFullPathName().toStdString(), std::ios::binary);
    writeUserFiles (shipped(), temp.dir, hostname, aes67Template());
    std::stringstream seen;
    seen << reader.rdbuf();
    EXPECT_EQ (seen.str(), "OLD=1\n");
    EXPECT_EQ (analyzerOf (temp.dir), analyzerBlock (shipped()));
}

TEST (Render, NoTemporaryFileIsLeftForTheAnalyzerToRead)
{
    recorded::TempDir temp;
    writeUserFiles (shipped(), temp.dir, hostname, aes67Template());
    juce::StringArray names;
    for (const auto& entry : juce::RangedDirectoryIterator (temp.dir.getChildFile (analyzerConfPath).getParentDirectory(), false))
        names.add (entry.getFile().getFileName());
    EXPECT_EQ (names.joinIntoString (","), "50-a3-osc.env");
}

TEST (Render, TheOldCheckoutEnvIsLeftAlone)
{
    // The package never reads build/.env; the rig's copy is carried over by hand, not rewritten here.
    recorded::TempDir temp;
    const auto old = temp.dir.getChildFile ("a3-system/beat-analyzer/build/.env");
    recorded::writeBytes (old, "LOG_LEVEL=1\n");
    writeUserFiles (shipped(), temp.dir, hostname, aes67Template());
    EXPECT_EQ (recorded::readBytes (old), "LOG_LEVEL=1\n");
}

// ---- the tool writes the files ----

TEST (Render, UserWritesTheEnvFileAndTheAnalyzersFile)
{
    recorded::TempDir temp;
    writeUserFiles (shipped(), temp.dir, hostname, aes67Template());
    EXPECT_EQ (recorded::readBytes (temp.dir.getChildFile (envFilePath)), oscEnv (shipped()));
    EXPECT_EQ (analyzerOf (temp.dir), analyzerBlock (shipped()));
    EXPECT_FALSE (temp.dir.getChildFile ("a3-system").exists());
}

TEST (Render, ARefusedNetworkFileIsSaid)
{
    // Final review 2026-10-02: the postinst would configure the interface from the package's values
    // without a word about the maintainer's file.
    recorded::TempDir temp;
    const auto broken = temp.dir.getChildFile ("network.json");
    recorded::writeBytes (broken, "{");
    auto loaded = Truth::load (truthFile (temp), broken);
    ASSERT_TRUE (std::holds_alternative<Truth> (loaded));
    const auto notice = refusedNetworkNotice (std::get<Truth> (loaded));
    ASSERT_TRUE (notice.has_value());
    EXPECT_NE (notice->find ("network.json"), std::string::npos);
    EXPECT_NE (notice->find ("refused"), std::string::npos);
    EXPECT_EQ (notice->rfind ("network.json refused, the package's defaults are used: ", 0), 0u);
    EXPECT_EQ (notice->back(), '\n');
    EXPECT_FALSE (refusedNetworkNotice (shipped()).has_value());
}

TEST (Render, NetworkPrintsShellAssignments)
{
    EXPECT_EQ (networkDefaults (shipped()), fixture ("texts/network-defaults"));
    EXPECT_EQ (pairs (networkDefaults (shipped())).size(), 6u);
}

TEST (Render, PostinstRendersForTheUser) { EXPECT_TRUE (someLineMatches (fixture ("package/postinst"), R"(a3-osc-render"? user)")); }

TEST (Render, PostinstCreatesTheNetworkFileAsTheUser)
{
    EXPECT_TRUE (someLineMatches (fixture ("package/postinst"), R"(sudo -u "\$APPUSER" -H [^\n]*a3-osc-render"? network-file)"));
}

TEST (Render, PostinstRendersTheUsersNetworkAsRoot)
{
    EXPECT_TRUE (someLineMatches (fixture ("package/postinst"),
                                  R"(A3_NETWORK="\$\{USER_HOME\}/\.config/a3/network\.json" [^\n]*a3-osc-render"? network$)"));
}

// ---- the network file ----

TEST (Render, ItHoldsTheTwoNetworkBlocks)
{
    const auto data = parsed (networkFile (shipped()));
    ASSERT_NE (data.getDynamicObject(), nullptr);
    EXPECT_EQ (data.getDynamicObject()->getProperties().size(), 2);
    EXPECT_TRUE (data.hasProperty ("hosts"));
    EXPECT_TRUE (data.hasProperty ("network"));
    EXPECT_EQ (data["hosts"]["mixer"].toString().toStdString(), shipped().host ("mixer"));
}

TEST (Render, ItIsCreatedOnceAndNeverOverwritten)
{
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("a3/network.json");
    EXPECT_TRUE (writeNetworkFileOnce (shipped(), path));
    recorded::writeBytes (path, "{\"mine\": true}");
    EXPECT_FALSE (writeNetworkFileOnce (shipped(), path));
    EXPECT_EQ (recorded::readBytes (path), "{\"mine\": true}");
}

TEST (Render, TheNetworkRenderReadsTheNamedFile)
{
    // The postinst renders Core's interface as root; A3_NETWORK points at the user's file, so
    // root's home is never read.
    recorded::TempDir temp;
    auto data = parsed (networkFile (shipped()));
    data["network"].getDynamicObject()->setProperty ("address", juce::String ("10.1.2.3/24"));
    const auto path = temp.dir.getChildFile ("network.json");
    recorded::writeBytes (path, dumpIndented (data, -1));
    const auto where = networkPath (false, temp.dir.getChildFile ("root-home"), path.getFullPathName().toRawUTF8());
    ASSERT_TRUE (where.has_value());
    auto loaded = Truth::load (truthFile (temp), where);
    ASSERT_TRUE (std::holds_alternative<Truth> (loaded));
    EXPECT_NE (networkDefaults (std::get<Truth> (loaded)).find ("10.1.2.3/24"), std::string::npos);
}

TEST (Render, NetworkFileAsPythonAndOnlyOnce)
{
    EXPECT_EQ (networkFile (shippedFile()), fixture ("texts/network.json"));
    recorded::TempDir temp;
    const auto path = temp.dir.getChildFile ("network.json");
    ASSERT_TRUE (writeNetworkFileOnce (shippedFile(), path));
    EXPECT_EQ (recorded::readBytes (path), fixture ("texts/network.json"));
    EXPECT_FALSE (writeNetworkFileOnce (shippedFile(), path));
    EXPECT_EQ (recorded::readBytes (path), fixture ("texts/network.json"));
}

// ---- the files, byte for byte as B0 wrote them ----

TEST (Render, EnvAndAnalyzerTextAsPython)
{
    EXPECT_EQ (oscEnv (shipped()), fixture ("texts/osc.env"));
    EXPECT_EQ (analyzerBlock (shipped()), fixture ("texts/analyzer.env"));
}

namespace
{
void copyTree (const juce::File& from, const juce::File& to)
{
    if (! from.isDirectory())
        return;
    for (const auto& entry : juce::RangedDirectoryIterator (from, true, "*", juce::File::findFiles))
    {
        const auto target = to.getChildFile (entry.getFile().getRelativePathFrom (from));
        target.getParentDirectory().createDirectory();
        ASSERT_TRUE (entry.getFile().copyFileTo (target));
    }
}

std::map<std::string, std::string> filesUnder (const juce::File& root)
{
    std::map<std::string, std::string> files;
    for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFiles))
        files[entry.getFile().getRelativePathFrom (root).toStdString()] = recorded::readBytes (entry.getFile());
    return files;
}

void expectHomeAsPython (const char* name, const Truth& truth)
{
    SCOPED_TRACE (name);
    const auto recordedCase = renderData.getChildFile ("homes").getChildFile (name);
    recorded::TempDir temp;
    copyTree (recordedCase.getChildFile ("before"), temp.dir);
    const auto message = writeUserFiles (truth, temp.dir, hostname, aes67Template());
    EXPECT_EQ (message, recorded::readBytes (recordedCase.getChildFile ("message.txt")));
    const auto got = filesUnder (temp.dir), want = filesUnder (recordedCase.getChildFile ("after"));
    EXPECT_FALSE (want.empty());
    EXPECT_EQ (got, want);
}
}

TEST (Render, UserFilesByteEqualToPython)
{
    expectHomeAsPython ("shipped", shipped());
    // The joined network has no audio port, and a rendered AES67 file waits: it goes, the rest stays.
    expectHomeAsPython ("removed", withoutAudioPort());
    // A config written by hand is never replaced.
    expectHomeAsPython ("hand-written", shipped());
    expectHomeAsPython ("empty", withoutAudioPort());
}

TEST (Render, TheAes67NameCarriesTheHostname)
{
    const auto text = aes67Conf (shipped(), "a3nuc2", aes67Template());
    ASSERT_TRUE (text.has_value());
    EXPECT_NE (text->find ("sess.name = \"AES67 a3nuc2 out\""), std::string::npos);
    EXPECT_EQ (text->find ('@'), std::string::npos);
    EXPECT_FALSE (aes67Conf (withoutAudioPort(), "a3nuc2", aes67Template()).has_value());
}

TEST (Render, OnlyTheFirstLineSaysWhoseAFileIs)
{
    recorded::TempDir temp;
    const auto file = temp.dir.getChildFile ("conf");
    const std::string header = renderedHeader;
    recorded::writeBytes (file, header + "\nmore\n");
    EXPECT_TRUE (isRendered (file));
    recorded::writeBytes (file, header);
    EXPECT_TRUE (isRendered (file));
    recorded::writeBytes (file, header + "\r\nmore\n");
    EXPECT_TRUE (isRendered (file));
    recorded::writeBytes (file, "# mine\n" + header + "\n");
    EXPECT_FALSE (isRendered (file));
    EXPECT_FALSE (isRendered (temp.dir.getChildFile ("missing")));
    EXPECT_FALSE (isRendered (temp.dir));
}
