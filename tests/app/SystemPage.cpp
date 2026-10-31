// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The SYSTEM page (2026-10-30: the Canvas's content in the Engine window): the project, the A³ System card from the
// umbrella checkout, the rig's rows from systemd and Core's /api/rig, and the plans that start and stop them. No test
// runs systemctl or git: a fake runner answers. Addresses come from the package's truth, never from here.
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <mutex>
#include "CommandRunner.h"
#include "CoreWindowRig.h"
#include "PageSnapshot.h"
#include "RigModel.h"
#include "RigPlan.h"
#include "SystemAbout.h"
#include "SystemCheckout.h"
#include "SystemPage.h"
#include "SystemProbe.h"

namespace
{
using a3app::Command;
namespace rig = a3app::rig;
namespace checkout = a3app::checkout;
namespace about = a3app::about;

juce::String joined (const Command& argv)
{
    juce::StringArray words;
    for (const auto& word : argv)
        words.add (word);
    return words.joinIntoString (" ");
}

// Answers commands by their whole line (a missing one: exit 1, nothing printed); keeps what ran.
struct ScriptedRunner final : a3app::CommandRunner
{
    std::map<juce::String, Result> answers;
    std::vector<juce::String> ran;
    std::mutex lock;
    Result run (const Command& argv) override
    {
        const std::lock_guard<std::mutex> guard (lock);
        ran.push_back (joined (argv));
        const auto found = answers.find (joined (argv));
        return found != answers.end() ? found->second : Result { 1, {} };
    }
    bool start (const Command&) override { return false; }
    std::optional<int> finished() override { return std::nullopt; }
};

// The package's truth: it names every machine of the rig (the test copy names only Core's).
const a3core::Truth& packageTruth()
{
    static const auto truth = [] {
        auto loaded = a3core::Truth::load (juce::File (A3_APP_SOURCE_DIR).getSiblingFile ("platform-config/debian-x86_64/a3-core/usr/share/a3/a3-osc.json"),
                                           std::nullopt);
        if (auto* problem = std::get_if<a3core::TruthError> (&loaded))
            throw std::runtime_error ("the package's truth: " + problem->what);
        return std::get<a3core::Truth> (std::move (loaded));
    }();
    return truth;
}

juce::String host (const char* name) { return juce::String (packageTruth().host (name)); }

rig::Network network() { return rig::truthNetwork (packageTruth().data()); }

// The rig of 2026-10-30 as the truth names it: this machine is the core (control and audio address).
rig::Network coreMachine()
{
    auto made = network();
    made.own = { host ("core"), host ("core-audio") };
    return made;
}

juce::String unitBlock (const juce::String& unit, const juce::String& active, const juce::String& load = "loaded")
{
    return "Id=" + unit + "\nLoadState=" + load + "\nActiveState=" + active + "\nSubState=x\nResult=success\n\n";
}

// /api/rig as Core says it: deck B on its own machine, the desk and Motion on Motion's, their reports where given.
juce::String rigBody (bool withReports)
{
    auto* deckA = new juce::DynamicObject();
    deckA->setProperty ("deck", 1); deckA->setProperty ("present", true); deckA->setProperty ("host", host ("deckhost-a"));
    auto* deckB = new juce::DynamicObject();
    deckB->setProperty ("deck", 2); deckB->setProperty ("present", true); deckB->setProperty ("host", host ("deckhost-b"));
    auto* desk = new juce::DynamicObject();
    desk->setProperty ("kind", "desk"); desk->setProperty ("host", host ("motion"));
    auto* motion = new juce::DynamicObject();
    motion->setProperty ("followed", host ("motion"));
    const auto device = [] (const juce::String& name, double age) {
        auto* row = new juce::DynamicObject();
        row->setProperty ("device", name); row->setProperty ("age", age);
        return juce::var (row);
    };
    const auto report = [] (const juce::String& name, const juce::String& from, const char* version, const char* firmware, const juce::String& audio) {
        auto* row = new juce::DynamicObject();
        row->setProperty ("device", name); row->setProperty ("host", from); row->setProperty ("version", version);
        row->setProperty ("firmware", firmware); row->setProperty ("audio", audio); row->setProperty ("age", 1.0);
        return juce::var (row);
    };
    auto* body = new juce::DynamicObject();
    body->setProperty ("decks", juce::Array<juce::var> { juce::var (deckA), juce::var (deckB) });
    body->setProperty ("desks", juce::Array<juce::var> { juce::var (desk) });
    body->setProperty ("motion", juce::var (motion));
    body->setProperty ("devices", juce::Array<juce::var> { device ("desk@" + host ("motion"), 3.0), device ("motion", 2.0), device ("mixer", 120.0) });
    juce::Array<juce::var> versions;
    if (withReports)
    {
        versions.add (report ("stemdeck", host ("deckhost-b"), "03.0+281", "", host ("deckhost-b-audio")));
        versions.add (report ("motion", host ("motion"), "03.0+855", "v03.0+80", ""));
        versions.add (report ("mixer", host ("mixer"), "03.0+40", "v03.0+12", ""));
        versions.add (report ("desk@" + host ("motion"), host ("motion"), "03.0+90", "", ""));
    }
    body->setProperty ("versions", versions);
    return juce::JSON::toString (juce::var (body));
}

const juce::String statusBody = R"({"audio_engine":"engine","reason":"","version":"03.0+857"})";

rig::Facts coreFacts (bool withReports)
{
    rig::Facts facts;
    facts.network = coreMachine();
    facts.core = rig::parseCore (rigBody (withReports), statusBody);
    juce::String units;
    for (const auto* unit : { "a3-engine.service", "a3-core.service", "stemdeck@A.service", "beat-analyzer.service" })
        units << unitBlock (unit, "active");
    units << unitBlock ("stemdeck@D.service", "inactive") << unitBlock ("a3-motion.service", "inactive", "not-found");
    facts.units = rig::parseSystemctlShow (units);
    facts.deskInstalled = false;
    return facts;
}

const rig::Program& programNamed (const std::vector<rig::Program>& list, const juce::String& label)
{
    for (const auto& program : list)
        if (program.label == label)
            return program;
    throw std::runtime_error (("no program " + label).toStdString());
}
}

TEST (SystemAbout, TheProjectAsTheCanvasSaidIt)
{
    EXPECT_EQ (about::tagline(), "Spatial DJ setup");
    EXPECT_EQ (about::copyrightLine(), juce::String::fromUTF8 ("© 2020–2026 Raphael Eismann and contributors"));
    EXPECT_EQ (about::contactAddress(), "p5hcu1@systemli.org");
    const auto links = about::links();
    ASSERT_GE (links.size(), 3u);
    EXPECT_EQ (links[0].label, "Documentation");
    EXPECT_EQ (links[1].label, "Website");
    EXPECT_EQ (links[2].label, "Source");
    EXPECT_TRUE (links[2].url.endsWith ("a3-audio/a3-system"));
    EXPECT_TRUE (about::licenceLine().contains ("GPL-3.0-or-later"));
}

TEST (SystemAbout, SupportIsOneFixedLinkTheSameForEveryone)
{
    EXPECT_EQ (about::supportUrl(), "https://www.paypal.com/paypalme/RaphaelEismann");
    const auto links = about::links();
    ASSERT_EQ (links.size(), 4u);
    EXPECT_EQ (links[3].label, juce::String::fromUTF8 ("Support A³"));
    EXPECT_EQ (links[3].url, about::supportUrl());
}

// a3-motion's board folder holds sub-projects (enc_pot, led_but) deeper down: the board is the shallowest project's folder.
TEST (SystemCheckout, ThePcbIsTheShallowestKicadProjectNotASubProject)
{
    const auto repo = juce::File::createTempFile ("a3-nested-pcb");
    const auto touch = [&repo] (const juce::String& path, const juce::String& content = {}) {
        const auto file = repo.getChildFile (path);
        file.getParentDirectory().createDirectory();
        file.replaceWithText (content);
    };
    touch ("hardware/pcbs/motion_pcb/enc_pot/enc_pot.kicad_pro");
    touch ("hardware/pcbs/motion_pcb/led_but/led_but.kicad_pro");
    touch ("hardware/pcbs/motion_pcb/motion_pcb.kicad_pro");
    touch ("hardware/pcbs/motion_pcb/REVISION", "v03\n");
    const auto parts = checkout::partsOf (repo);
    ASSERT_EQ (parts.size(), 1u);
    EXPECT_EQ (parts[0].name, "pcb");
    EXPECT_EQ (parts[0].folder, "hardware/pcbs/motion_pcb");
    EXPECT_EQ (parts[0].revision, "v03");
    repo.deleteRecursively();
}

// stemseparator is pinned but runs off the rig and is never tagged (release.py leaves it out): not a part of the system.
TEST (SystemCheckout, ARepoOutsideTheReleaseIsNotInTheTree)
{
    EXPECT_TRUE (checkout::isInRelease ("a3-core"));
    EXPECT_TRUE (checkout::isInRelease ("stemdeck"));
    EXPECT_FALSE (checkout::isInRelease ("stemseparator"));
}

TEST (SystemCheckout, ReadsTheUmbrellasPinsAndFirmware)
{
    EXPECT_EQ (checkout::releaseText ("v03.0-413-gabc1234"), "v03.0+413");
    EXPECT_EQ (checkout::releaseText ("v03.0"), "v03.0");
    EXPECT_EQ (checkout::releaseText ("abc1234"), "abc1234");
    EXPECT_EQ (checkout::tagText ({ "stemdeck", "" }), "not checked out");

    const juce::File repo ("/home/someone/a3-system");
    const auto git = [&repo] (const juce::String& dir, const juce::String& rest) {
        return "git -C " + (dir.isEmpty() ? repo : repo.getChildFile (dir)).getFullPathName() + " " + rest;
    };
    ScriptedRunner runner;
    runner.answers[git ("", "ls-tree HEAD")] = { 0, "160000 commit aaa\ta3-core\n100644 blob bbb\tREADME.md\n160000 commit ccc\ta3-motion\n"
                                                    "160000 commit ddd\tstemdeck\n" };
    runner.answers[git ("", "describe --tags --match v* --always HEAD")] = { 0, "v03.0-12-g1234567\n" };
    runner.answers[git ("a3-core", "describe --tags --match v* --always aaa")] = { 0, "v03.0-857-gaaaaaaa\n" };
    runner.answers[git ("a3-motion", "describe --tags --match v* --always ccc")] = { 0, "v03.0-855-gccccccc\n" };
    runner.answers[git ("a3-motion", "log -1 --format=%H ccc -- firmware")] = { 0, "fff\n" };
    runner.answers[git ("a3-motion", "describe --tags --match v* --abbrev=0 ccc")] = { 0, "v03.0\n" };
    runner.answers[git ("a3-motion", "rev-list --count v03.0..ccc -- firmware")] = { 0, "2\n" };
    runner.answers[git ("a3-motion", "ls-tree ccc")] = { 0, "160000 commit eee\tui\n" };
    runner.answers[git ("a3-motion/ui", "describe --tags --match v* --always eee")] = { 0, "v03.0-900-geeeeee\n" };
    // Where each is published: the umbrella's .gitmodules at its HEAD, a3-motion's at its pin (a relative url is its sibling).
    runner.answers[git ("", "config --blob HEAD:.gitmodules --get-regexp ^submodule\\..*\\.url$")]
        = { 0, "submodule.a3-core.url https://github.com/a3-audio/a3-core\nsubmodule.a3-motion.url https://github.com/a3-audio/a3-motion\n"
               "submodule.stemdeck.url https://github.com/rafjagger/stemdeck\n" };
    runner.answers[git ("a3-motion", "config --blob ccc:.gitmodules --get-regexp ^submodule\\..*\\.url$")]
        = { 0, "submodule.ui.url ../a3-motion-ui.git\n" };
    const auto facts = checkout::read (runner, repo);
    EXPECT_TRUE (facts.found);
    EXPECT_EQ (facts.version, "v03.0+12");
    const std::vector<checkout::Row> expected {
        { "a3-core", "v03.0+857", "https://github.com/a3-audio/a3-core" },
        { "a3-motion", "v03.0+855", "https://github.com/a3-audio/a3-motion" },
        { "a3-motion firmware", "rev v03.0+2", "https://github.com/a3-audio/a3-motion/tree/HEAD/firmware" },
        { "a3-motion/ui", "v03.0+900", "https://github.com/a3-audio/a3-motion-ui" },
        { "stemdeck", "", "https://github.com/rafjagger/stemdeck" } };
    EXPECT_EQ (facts.rows, expected);
    for (const auto& line : runner.ran)
        EXPECT_TRUE (line.startsWith ("git -C ")) << "only reads: " << line;

    ScriptedRunner nothing;
    EXPECT_FALSE (checkout::read (nothing, repo).found);
}

// The licences column (2026-10-31): the SPDX ids of a repository's LICENSES/ folder, the project's own ones, short; a
// firmware's from the REUSE stanza that covers it, else from the source files' SPDX headers.
TEST (CheckoutLicences, AreReadFromTheLicensesFolderAndForTheFirmwareFromReuseData)
{
    const auto root = juce::File::createTempFile ("a3-licences");
    const auto touch = [&root] (const juce::String& path, const juce::String& content = {}) {
        const auto file = root.getChildFile (path);
        file.getParentDirectory().createDirectory();
        file.replaceWithText (content);
    };
    for (const auto* id : { "GPL-3.0-or-later", "CC0-1.0", "MIT", "CERN-OHL-S-2.0", "CC-BY-SA-4.0", "Apache-2.0", "LicenseRef-SnapMagic" })
        touch ("a3-mixer/LICENSES/" + juce::String (id) + ".txt");
    EXPECT_EQ (checkout::codeLicence (root.getChildFile ("a3-mixer")), "GPL-3.0") << "the code's; CC0, MIT, Apache and LicenseRef are left out";
    touch ("a3-doc/LICENSES/CC-BY-SA-4.0.txt");
    EXPECT_EQ (checkout::codeLicence (root.getChildFile ("a3-doc")), "CC-BY-SA-4.0") << "a repository of documentation says so";
    EXPECT_TRUE (checkout::codeLicence (root.getChildFile ("not-here")).isEmpty()) << "not checked out";
    EXPECT_EQ (checkout::shortLicence ("GPL-3.0-or-later"), "GPL-3.0");
    EXPECT_EQ (checkout::shortLicence ("CERN-OHL-S-2.0"), "CERN-OHL-S");
    EXPECT_TRUE (checkout::shortLicence ("CC0-1.0").isEmpty());

    // From a dep5 stanza: the one that covers the folder, a Markdown and an ini stanza not counting.
    touch ("a3-motion/.reuse/dep5",
           "Format: x\n\nFiles: *\nCopyright: a\nLicense: CC-BY-SA-4.0\n\nFiles: firmware/*\nCopyright: a\nLicense: GPL-3.0-or-later\n\n"
           "Files: firmware/*.md\nCopyright: a\nLicense: CC-BY-SA-4.0\n\nFiles: firmware/platformio.ini\nCopyright: a\nLicense: CC0-1.0\n");
    EXPECT_EQ (checkout::licenceOfFolder (root.getChildFile ("a3-motion"), "firmware"), "GPL-3.0");

    // Without a stanza: the SPDX headers of the source files.
    touch ("a3-mixer/firmware/src/main.cpp", "// SPDX-FileCopyrightText: 2024 x\n// SPDX-License-Identifier: CERN-OHL-S-2.0\n");
    touch ("a3-mixer/firmware/src/more.h", "// SPDX-License-Identifier: CERN-OHL-S-2.0\n");
    touch ("a3-mixer/.reuse/dep5", "Format: x\n\nFiles: docs/*\nCopyright: a\nLicense: CC-BY-SA-4.0\n");
    EXPECT_EQ (checkout::licenceOfFolder (root.getChildFile ("a3-mixer"), "firmware"), "CERN-OHL-S");
    root.deleteRecursively();
}

TEST (RigModel, TheProgramsAreTheCanvassRows)
{
    const auto list = rig::programs (4);
    juce::StringArray labels;
    for (const auto& program : list)
        labels.add (program.label);
    EXPECT_EQ (labels, (juce::StringArray { juce::String ("a3-core"), "Deck A", "Deck B", "Deck C", "Deck D",
                                            juce::String::fromUTF8 ("A³ Mixer"), juce::String::fromUTF8 ("A³ Motion"), "Beat analyzer" }));
    EXPECT_EQ (list[0].units, (std::vector<juce::String> { "a3-engine.service", "a3-core.service" })) << "never the window's own unit";
    EXPECT_EQ (list[2].units, (std::vector<juce::String> { "stemdeck@B.service" }));
    const auto show = rig::showCommand (list);
    EXPECT_EQ (show[0], "systemctl");
    EXPECT_EQ (show[1], "--user");
}

// Each row's software, firmware and audio address from what the device reported to Core; "not reported" only where it
// said nothing. Where it runs from Core's word, else the truth's hosts.
TEST (RigModel, RowsSayWhatEachDeviceReports)
{
    const auto list = rig::programs (4);
    const auto facts = coreFacts (true);

    const auto engine = rig::describe (programNamed (list, juce::String ("a3-core")), facts);
    EXPECT_TRUE (engine.here);
    EXPECT_EQ (engine.where, juce::String::fromUTF8 ("local · ") + host ("core"));
    EXPECT_EQ (engine.audio, host ("core-audio"));
    EXPECT_EQ (engine.software, "03.0+857") << "Core's own version, from /api/status";
    EXPECT_EQ (engine.state, "Running");
    EXPECT_TRUE (engine.up);
    EXPECT_TRUE (engine.firmware.isEmpty());

    const auto deckB = rig::describe (programNamed (list, "Deck B"), facts);
    EXPECT_FALSE (deckB.here);
    EXPECT_EQ (deckB.where, host ("deckhost-b"));
    EXPECT_EQ (deckB.software, "03.0+281");
    EXPECT_EQ (deckB.audio, host ("deckhost-b-audio"));
    EXPECT_EQ (deckB.state, "Heard by Core");

    const auto deckA = rig::describe (programNamed (list, "Deck A"), facts);
    EXPECT_TRUE (deckA.here);
    EXPECT_EQ (deckA.software, rig::notReported) << "deck B's report is not deck A's";

    const auto deckD = rig::describe (programNamed (list, "Deck D"), facts);
    EXPECT_EQ (deckD.state, "Stopped");
    EXPECT_FALSE (deckD.up);

    const auto motion = rig::describe (programNamed (list, juce::String::fromUTF8 ("A³ Motion")), facts);
    EXPECT_FALSE (motion.here);
    EXPECT_EQ (motion.where, host ("motion"));
    EXPECT_EQ (motion.software, "03.0+855");
    EXPECT_EQ (motion.firmware, "v03.0+80");
    EXPECT_EQ (motion.audio, host ("motion-audio")) << "the report has none: the truth's";

    const auto mixer = rig::describe (programNamed (list, juce::String::fromUTF8 ("A³ Mixer")), facts);
    EXPECT_EQ (mixer.software, "03.0+90") << "the desk's report";
    EXPECT_EQ (mixer.firmware, "v03.0+12") << "the Pico's, as the mixer script reports it";
    EXPECT_EQ (mixer.state, "Heard by Core");

    const auto silent = coreFacts (false);
    const auto quiet = rig::describe (programNamed (list, juce::String::fromUTF8 ("A³ Motion")), silent);
    EXPECT_EQ (quiet.software, rig::notReported);
    EXPECT_EQ (quiet.firmware, rig::notReported);
    EXPECT_EQ (rig::describe (programNamed (list, "Beat analyzer"), silent).software, rig::notReported);
}

TEST (RigModel, UnitsSayTheStateHere)
{
    const auto list = rig::programs (4);
    auto facts = coreFacts (true);
    facts.units = rig::parseSystemctlShow (unitBlock ("a3-engine.service", "failed") + unitBlock ("a3-core.service", "active"));
    EXPECT_EQ (rig::describe (list[0], facts).light, rig::Light::failed);
    EXPECT_TRUE (rig::describe (list[0], facts).state.startsWith ("a3-engine failed"));
    facts.units = rig::parseSystemctlShow (unitBlock ("a3-engine.service", "activating") + unitBlock ("a3-core.service", "active"));
    EXPECT_EQ (rig::describe (list[0], facts).light, rig::Light::starting);
    facts.units = rig::parseSystemctlShow (unitBlock ("a3-engine.service", "active") + unitBlock ("a3-core.service", "inactive"));
    EXPECT_EQ (rig::describe (list[0], facts).state, "a3-core not running");
    facts.units = rig::parseSystemctlShow (unitBlock ("beat-analyzer.service", "inactive", "not-found"));
    const auto analyzer = rig::describe (programNamed (list, "Beat analyzer"), facts);
    EXPECT_EQ (analyzer.state, "Not installed");
    EXPECT_FALSE (analyzer.installed);

    facts.core = rig::parseCore ({}, {});
    EXPECT_FALSE (facts.core.answers);
    EXPECT_EQ (rig::describe (programNamed (list, "Deck B"), facts).where, host ("deckhost-b")) << "the truth's machine";
    EXPECT_EQ (rig::describe (programNamed (list, "Deck B"), facts).state, "Core not answering");
}

TEST (RigModel, OwnAddressesSkipBridgesAndLoopback)
{
    const auto net = network();
    const auto own = rig::ownAddresses ({ { "lo", "127.0.0.1" }, { "docker0", "172.17.0.1" }, { "enp5s0", host ("core-audio") },
                                          { "eno1", host ("core") } }, net);
    EXPECT_EQ (own, (std::vector<juce::String> { host ("core"), host ("core-audio") })) << "control first";
}

namespace
{
// dpkg-query -L a3-core a3-core-app as on the rig (2026-10-31), cut to the unit files and a few others.
const juce::String rigPackageFiles = "/etc/systemd/system/a3-ptp-state.service\n/etc/systemd/system/a3-ptp-state.timer\n"
                                     "/home/aaa/.local/share/a3-core/config/systemd/user/a3-aes67.service\n"
                                     "/home/aaa/.local/share/a3-core/config/systemd/user/a3-core.service\n"
                                     "/home/aaa/.local/share/a3-core/config/systemd/user/a3-main.service\n"
                                     "/home/aaa/.local/share/a3-core/config/systemd/user/default.target.wants/a3-main.service\n"
                                     "/usr/share/a3/a3-osc.json\n/usr/lib/systemd/user/a3-clock.service\n"
                                     "/usr/lib/systemd/user/a3-core-window.service\n/usr/lib/systemd/user/a3-engine.service\n"
                                     "/usr/lib/systemd/user/a3-core.service\n";
const juce::String rigMainWants = "stemdeck@A.service a3-core.service a3-engine.service a3-motion.service beat-analyzer.service a3-core-window.service\n";
}

// The a3-core card's services (2026-10-31): the a3-core and a3-core-app packages' unit files and what a3-main Wants, less
// the units that are another row's (deck A, Motion); the beat analyzer is the Core's.
TEST (RigModel, TheCoresServicesComeFromThePackagesAndA3Main)
{
    const auto packaged = rig::packageUnits (rigPackageFiles);
    EXPECT_EQ (packaged.size(), 8u) << "a .wants link is no unit of its own, a3-core.service once";
    const auto services = rig::coreServices (packaged, rig::parseWants (rigMainWants), rig::programs (4));
    juce::StringArray names;
    for (const auto& service : services)
        names.add (service.name + (service.system ? " (system)" : ""));
    EXPECT_EQ (names, (juce::StringArray { "a3-aes67.service", "a3-clock.service", "a3-core-window.service", "a3-core.service",
                                           "a3-engine.service", "a3-main.service", "beat-analyzer.service",
                                           "a3-ptp-state.timer (system)" })) << "a service with a timer is read by its timer";
    const auto system = rig::showCommand (services, true);
    EXPECT_EQ (system[1], "show") << "the system's units without --user";
}

// Grey only for a unit that should run and does not (2026-10-31): enabled, static or wanted by a3-main and inactive; a
// disabled one that does not run is fine (no hue), active is green, failed red.
TEST (RigModel, OnlyAUnitThatShouldRunAndDoesNotIsGrey)
{
    rig::Facts facts;
    facts.network = coreMachine();
    facts.coreServices = { { "beat-analyzer.service", false }, { "set_irq_prio.service", true }, { "spare.service", true },
                           { "x11vnc.service", true }, { "broken.service", true } };
    facts.mainWants = { "beat-analyzer.service" };
    facts.units = rig::parseSystemctlShow (unitBlock ("beat-analyzer.service", "inactive").replace ("Result=success", "Result=success\nUnitFileState=static"));
    facts.systemUnits = rig::parseSystemctlShow (
        unitBlock ("set_irq_prio.service", "inactive").replace ("Result=success", "Result=success\nUnitFileState=enabled")
        + unitBlock ("spare.service", "inactive").replace ("Result=success", "Result=success\nUnitFileState=disabled")
        + unitBlock ("x11vnc.service", "active").replace ("Result=success", "Result=success\nUnitFileState=enabled")
        + unitBlock ("broken.service", "failed").replace ("Result=success", "Result=success\nUnitFileState=enabled"));
    const auto row = rig::describe (rig::programs (1)[0], facts);
    ASSERT_EQ (row.services.size(), 5u);
    EXPECT_EQ (row.services[0].light, rig::Light::stopped) << "wanted by a3-main, not running: grey";
    EXPECT_EQ (row.services[1].light, rig::Light::stopped) << "enabled, not running: grey";
    EXPECT_EQ (row.services[2].light, rig::Light::idle) << "disabled and not running: fine";
    EXPECT_EQ (row.services[3].light, rig::Light::running);
    EXPECT_EQ (row.services[4].light, rig::Light::failed);
}

TEST (RigModel, TheServiceSummaryNamesWhatIsWrong)
{
    using L = rig::Light;
    EXPECT_EQ (rig::summarize ({ { "a3-engine.service", "active", L::running }, { "a3-ptp.timer", "active", L::running }, { "off.service", "inactive", L::idle } }).text,
               juce::String::fromUTF8 ("3 services · all running"));
    const auto one = rig::summarize ({ { "a3-engine.service", "active", L::running }, { "a3-aes67.service", "inactive", L::stopped } });
    EXPECT_EQ (one.text, juce::String::fromUTF8 ("1 not running: a3-aes67"));
    EXPECT_EQ (one.worst, L::stopped);
    const auto many = rig::summarize ({ { "a.service", "failed", L::failed }, { "b.service", "x", L::stopped }, { "c.service", "x", L::stopped }, { "d.service", "x", L::running } });
    EXPECT_EQ (many.text, "3 not running: a, b +1");
    EXPECT_EQ (many.worst, L::failed);
    EXPECT_EQ (rig::summarize ({ { "a.service", "not read yet", L::unknown } }).text, juce::String::fromUTF8 ("1 service · not read yet"));
}

TEST (RigPlan, StartAllInTheRigsOrderAndOnlyWhatRunsHere)
{
    const auto list = rig::programs (2);
    const std::vector<rig::Program> here { list[0], list[1], programNamed (list, "Beat analyzer") };
    juce::StringArray lines;
    for (const auto& step : rig::startAll (here))
        lines.add (joined (step.argv));
    EXPECT_EQ (lines, (juce::StringArray { "systemctl --user import-environment DISPLAY XAUTHORITY", "systemctl --user start a3-engine.service",
                                           "systemctl --user start a3-core.service", "systemctl --user start beat-analyzer.service",
                                           "systemctl --user start stemdeck@A.service" }));
    lines.clear();
    for (const auto& step : rig::stopAll (here))
        lines.add (joined (step.argv));
    EXPECT_EQ (lines, (juce::StringArray { "systemctl --user stop stemdeck@A.service", "systemctl --user stop beat-analyzer.service",
                                           "systemctl --user stop a3-core.service", "systemctl --user stop a3-engine.service" }));
    const auto desk = rig::restart (programNamed (list, juce::String::fromUTF8 ("A³ Mixer")));
    EXPECT_EQ (joined (desk.back().argv), "systemd-run --user --unit=a3-desk --collect /usr/bin/a3-desk");
}

TEST (RigPlan, AFailedEngineSkipsTheDecks)
{
    const auto list = rig::programs (1);
    ScriptedRunner runner;
    runner.answers["systemctl --user import-environment DISPLAY XAUTHORITY"] = { 0, {} };
    runner.answers["systemctl --user start a3-engine.service"] = { 0, {} };
    runner.answers["systemctl --user start a3-core.service"] = { 0, {} };
    std::vector<juce::String> log;
    const auto result = rig::execute (rig::startAll ({ list[0], list[1] }), runner,
                                      [] (const juce::String& unit) { return unit == "a3-engine.service" ? "failed" : "active"; },
                                      { 3, [] {} }, [&log] (const juce::String& line) { log.push_back (line); });
    EXPECT_EQ (result.failed, (std::vector<juce::String> { "a3-engine.service" }));
    EXPECT_EQ (result.skipped, (std::vector<juce::String> { "stemdeck@A.service" }));
    EXPECT_EQ (rig::outcome ("Start all", result), "Start all: 1 failed (a3-engine.service), 1 skipped");
    EXPECT_EQ (rig::outcome ("Start all", {}), "Start all: done");
}

namespace
{
a3app::SystemSources fakeSources (std::shared_ptr<ScriptedRunner> runner)
{
    a3app::SystemSources sources;
    sources.runner = runner;
    sources.planRunner = runner;
    sources.askCore = [] { return std::make_pair (rigBody (true), statusBody); };
    sources.interfaces = [] { return std::vector<rig::InterfaceAddress> { { "eno1", host ("core") }, { "enp5s0", host ("core-audio") } }; };
    sources.truth = network();
    sources.checkout = juce::File ("/home/someone/a3-system");
    sources.decks = 4;
    sources.deskInstalled = [] { return false; };
    return sources;
}

std::shared_ptr<ScriptedRunner> rigRunner()
{
    auto runner = std::make_shared<ScriptedRunner>();
    juce::String units;
    for (const auto* unit : { "a3-engine.service", "a3-core.service", "stemdeck@A.service", "beat-analyzer.service" })
        units << unitBlock (unit, "active");
    units << unitBlock ("stemdeck@D.service", "inactive");
    runner->answers[joined (rig::showCommand (rig::programs (4)))] = { 0, units.toStdString() };
    runner->answers[joined (rig::packageUnitsCommand())] = { 0, rigPackageFiles.toStdString() };
    runner->answers[joined (rig::mainWantsCommand())] = { 0, rigMainWants.toStdString() };
    const auto services = rig::coreServices (rig::packageUnits (rigPackageFiles), rig::parseWants (rigMainWants), rig::programs (4));
    juce::String userUnits, systemUnits;
    for (const auto& service : services)
        (service.system ? systemUnits : userUnits)
            << unitBlock (service.name, service.name == "a3-clock.service" ? "failed" : "active").replace ("Result=success", "Result=success\nUnitFileState=enabled");
    runner->answers[joined (rig::showCommand (services, false))] = { 0, userUnits.toStdString() };
    runner->answers[joined (rig::showCommand (services, true))] = { 0, systemUnits.toStdString() };
    const juce::File repo ("/home/someone/a3-system");
    runner->answers["git -C " + repo.getFullPathName() + " ls-tree HEAD"] = { 0, "160000 commit aaa\ta3-core\n160000 commit ddd\tstemdeck\n" };
    runner->answers["git -C " + repo.getFullPathName() + " describe --tags --match v* --always HEAD"] = { 0, "v03.0-12-g1234567\n" };
    runner->answers["git -C " + repo.getChildFile ("a3-core").getFullPathName() + " describe --tags --match v* --always aaa"] = { 0, "v03.0-857-gaaaaaaa\n" };
    runner->answers["git -C " + repo.getFullPathName() + " config --blob HEAD:.gitmodules --get-regexp ^submodule\\..*\\.url$"]
        = { 0, "submodule.a3-core.url https://github.com/a3-audio/a3-core\nsubmodule.stemdeck.url https://github.com/rafjagger/stemdeck\n" };
    return runner;
}

struct SystemPageTest : ::testing::Test
{
    juce::MessageManagerLock mm;
    a3app::Look look;
    SystemPageTest() { juce::LookAndFeel::setDefaultLookAndFeel (&look); }
    ~SystemPageTest() override { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
};
}

TEST_F (SystemPageTest, TheProbeReadsTheMachineOnlyWhenAsked)
{
    auto runner = rigRunner();
    a3app::SystemProbe probe (fakeSources (runner), false);
    EXPECT_FALSE (probe.snapshot().polled);
    EXPECT_TRUE (runner->ran.empty()) << "nothing asked before the page is shown";
    probe.pollNow();
    const auto snapshot = probe.snapshot();
    EXPECT_TRUE (snapshot.polled);
    EXPECT_TRUE (snapshot.facts.core.answers);
    EXPECT_EQ (snapshot.checkout.version, "v03.0+12");
    EXPECT_EQ (snapshot.facts.network.own.front(), host ("core"));
}

TEST_F (SystemPageTest, RigRowsKeysAndPlans)
{
    auto runner = rigRunner();
    a3app::SystemProbe probe (fakeSources (runner), false);
    probe.pollNow();
    std::vector<juce::String> asked;
    std::function<void()> lastYes;
    a3app::SystemRigView rigView (probe, [&] (const juce::String& question, const juce::String&, std::function<void()> yes) {
        asked.push_back (question);
        lastYes = std::move (yes);
    });
    rigView.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    rigView.refresh();
    const auto engine = juce::String ("a3-core");
    EXPECT_EQ (rigView.stateLine (engine), "Running");
    EXPECT_TRUE (rigView.detailLine (engine).contains ("sw 03.0+857")) << rigView.detailLine (engine);
    EXPECT_TRUE (rigView.detailLine ("Deck B").contains ("sw 03.0+281")) << rigView.detailLine ("Deck B");
    EXPECT_TRUE (rigView.detailLine (juce::String::fromUTF8 ("A³ Motion")).contains ("fw v03.0+80"));
    EXPECT_EQ (rigView.startStopKey ("Deck B"), nullptr) << "another machine's program has no key here";
    ASSERT_NE (rigView.startStopKey (engine), nullptr);
    EXPECT_EQ (rigView.startStopKey (engine)->getButtonText(), "Stop");
    ASSERT_NE (rigView.startStopKey ("Deck D"), nullptr);
    EXPECT_EQ (rigView.startStopKey ("Deck D")->getButtonText(), "Start");
    // The a3-core card lists every unit of the Core here, the beat analyzer among them (no row of its own then).
    const auto services = rigView.serviceLines();
    EXPECT_TRUE (services.contains (juce::String::fromUTF8 ("a3-engine.service  active · enabled"))) << services.joinIntoString (" | ");
    EXPECT_TRUE (services.contains (juce::String::fromUTF8 ("a3-clock.service  failed · enabled"))) << services.joinIntoString (" | ");
    EXPECT_TRUE (services.contains (juce::String::fromUTF8 ("a3-ptp-state.timer  active · enabled · system"))) << services.joinIntoString (" | ");
    EXPECT_TRUE (services.contains (juce::String::fromUTF8 ("beat-analyzer.service  active · enabled"))) << services.joinIntoString (" | ");
    EXPECT_FALSE (rigView.hasOwnRow ("Beat analyzer"));
    EXPECT_TRUE (rigView.hasOwnRow ("Deck A"));
    // 2026-10-31: MAIN, then ENTITIES with a row per program, its name on it.
    EXPECT_EQ (rigView.programRows(), (int) probe.programs().size());
    ASSERT_EQ (rigView.cardAreas().size(), 2u) << "MAIN and ENTITIES";
    EXPECT_TRUE (rigView.cardAreas()[1].contains (rigView.entitiesView())) << "the rows scroll inside ENTITIES";
    for (const auto& program : probe.programs())
    {
        bool named = false;
        for (auto* child : rigView.programList().getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
                named = named || label->getText() == program.label;
        EXPECT_TRUE (named) << program.label;
    }
    // The services are one summary line, closed at first; a tap on the key opens the list and another closes it.
    EXPECT_FALSE (rigView.servicesOpen());
    EXPECT_TRUE (rigView.serviceSummary().contains ("not running: a3-clock")) << rigView.serviceSummary() << ": the fault is said while closed";
    a3apptest::expectPainted (a3apptest::snapshotAt (rigView, "system3-rig-services-closed", rigView.getWidth(), rigView.getHeight()));
    rigView.toggleServices();
    EXPECT_TRUE (rigView.servicesOpen());
    a3apptest::expectPainted (a3apptest::snapshotAt (rigView, "system3-rig", rigView.getWidth(), rigView.getHeight()));
    rigView.refresh();
    EXPECT_TRUE (rigView.servicesOpen()) << "kept across refreshes";
    rigView.toggleServices();
    EXPECT_FALSE (rigView.servicesOpen());

    // Stop asks first; only a yes runs it.
    rigView.startStopKey ("Deck A")->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    ASSERT_EQ (asked.size(), 1u);
    EXPECT_EQ (asked.back(), "Stop Deck A?");
    runner->ran.clear();
    runner->answers["systemctl --user stop stemdeck@A.service"] = { 0, {} };
    lastYes();
    probe.waitForPlan();
    EXPECT_EQ (runner->ran, (std::vector<juce::String> { "systemctl --user stop stemdeck@A.service" }));
    EXPECT_EQ (probe.snapshot().planLine, "Stop Deck A: done");

    // Start of a stopped deck runs at once.
    runner->ran.clear();
    rigView.startStopKey ("Deck D")->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    probe.waitForPlan();
    EXPECT_EQ (asked.size(), 1u) << "a start is not asked";
    EXPECT_TRUE (std::find (runner->ran.begin(), runner->ran.end(), "systemctl --user start stemdeck@D.service") != runner->ran.end());
    rigView.refresh();
    EXPECT_TRUE (rigView.planLine().startsWith ("Start Deck D")) << rigView.planLine();
}

// The tree with its three columns: connectors and names, versions, licences (from fixture folders, as on the rig).
TEST_F (SystemPageTest, AboutShowsTheTreeWithVersionsAndLicences)
{
    const auto repo = juce::File::createTempFile ("a3-about-tree");
    const auto touch = [&repo] (const juce::String& path, const juce::String& content = {}) {
        const auto file = repo.getChildFile (path);
        file.getParentDirectory().createDirectory();
        file.replaceWithText (content);
    };
    for (const auto* id : { "GPL-3.0-or-later", "CC-BY-SA-4.0", "CC0-1.0", "MIT" })
        touch ("a3-core/LICENSES/" + juce::String (id) + ".txt");
    touch ("LICENSES/GPL-3.0-or-later.txt");   // the umbrella's own
    touch ("LICENSES/CC0-1.0.txt");
    for (const auto* id : { "GPL-3.0-or-later", "CC-BY-SA-4.0", "CERN-OHL-S-2.0", "CC0-1.0" })
    {
        touch ("a3-mixer/LICENSES/" + juce::String (id) + ".txt");
        touch ("a3-motion/LICENSES/" + juce::String (id) + ".txt");
    }
    touch ("a3-motion/ui/LICENSES/GPL-3.0-or-later.txt");
    // The Mixer laid out like Motion (2026-10-31): board and housing under hardware/ (CERN), each with its REVISION;
    // the firmware top level, by its stanza.
    touch ("a3-mixer/.reuse/dep5", "Format: x\n\nFiles: hardware/*\nCopyright: a\nLicense: CERN-OHL-S-2.0\n\n"
                                   "Files: firmware/*\nCopyright: a\nLicense: GPL-3.0-or-later\n");
    touch ("a3-mixer/hardware/pcb/board.kicad_pro");
    touch ("a3-mixer/hardware/pcb/REVISION", "V02\n");   // upper case in the file, lower case on the card
    touch ("a3-mixer/hardware/housing/box.blend");
    touch ("a3-mixer/hardware/housing/REVISION", "V02\n");
    touch ("a3-mixer/firmware/src/main.cpp", "// SPDX-License-Identifier: GPL-3.0-or-later\n");
    // Motion: a board with a revision, a housing without one, the firmware by its stanza.
    touch ("a3-motion/.reuse/dep5", "Format: x\n\nFiles: hardware/*\nCopyright: a\nLicense: CERN-OHL-S-2.0\n\nFiles: firmware/*\nCopyright: a\nLicense: GPL-3.0-or-later\n");
    touch ("a3-motion/hardware/pcbs/motion_pcb/motion.kicad_pro");
    touch ("a3-motion/hardware/pcbs/motion_pcb/REVISION", "v03\n");
    touch ("a3-motion/hardware/housing/freecad/box.FCStd");
    touch ("a3-motion/firmware/src/main.cpp");
    auto runner = rigRunner();
    const auto git = [&repo] (const juce::String& dir, const juce::String& rest) {
        return "git -C " + (dir.isEmpty() ? repo : repo.getChildFile (dir)).getFullPathName() + " " + rest;
    };
    runner->answers[git ("", "ls-tree HEAD")] = { 0, "160000 commit aaa\ta3-core\n160000 commit bbb\ta3-mixer\n160000 commit ccc\ta3-motion\n160000 commit ddd\tstemdeck\n160000 commit sss\tstemseparator\n" };
    runner->answers[git ("", "describe --tags --match v* --always HEAD")] = { 0, "v03.0-12-g1234567\n" };
    runner->answers[git ("a3-core", "describe --tags --match v* --always aaa")] = { 0, "v03.0-857-gaaaaaaa\n" };
    runner->answers[git ("a3-mixer", "describe --tags --match v* --always bbb")] = { 0, "v03.0-90-gbbbbbbb\n" };
    runner->answers[git ("a3-mixer", "log -1 --format=%H bbb -- firmware")] = { 0, "f1\n" };
    runner->answers[git ("a3-mixer", "describe --tags --match v* --abbrev=0 bbb")] = { 0, "v03.0\n" };
    runner->answers[git ("a3-mixer", "rev-list --count v03.0..bbb -- firmware")] = { 0, "0\n" };   // unchanged since the release
    runner->answers[git ("a3-motion", "describe --tags --match v* --always ccc")] = { 0, "v03.0-855-gccccccc\n" };
    runner->answers[git ("a3-motion", "log -1 --format=%H ccc -- firmware")] = { 0, "f2\n" };
    runner->answers[git ("a3-motion", "describe --tags --match v* --abbrev=0 ccc")] = { 0, "v03.0\n" };
    runner->answers[git ("a3-motion", "rev-list --count v03.0..ccc -- firmware")] = { 0, "2\n" };   // changed twice since
    runner->answers[git ("a3-motion", "ls-tree ccc")] = { 0, "160000 commit eee\tui\n" };
    runner->answers[git ("a3-motion/ui", "describe --tags --match v* --always eee")] = { 0, "v03.0-900-geeeeee\n" };
    auto sources = fakeSources (runner);
    sources.checkout = repo;
    a3app::SystemProbe probe (sources, false);
    probe.pollNow();
    a3app::SystemAboutView view (probe);
    view.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    view.refresh();
    EXPECT_EQ (view.checkoutShown().licence, "GPL-3.0") << "the umbrella's own";
    juce::StringArray lines;
    for (const auto& row : view.checkoutShown().rows)
        lines.add (row.name + "|" + row.tag + "|" + row.licences + (row.noVersion ? "|no version" : ""));
    EXPECT_EQ (lines.joinIntoString ("\n"),
               juce::String ("a3-core|v03.0+857|GPL-3.0\n"
                             "a3-mixer|v03.0+90|GPL-3.0\na3-mixer pcb|rev v02|CERN-OHL-S\na3-mixer housing|rev v02|CERN-OHL-S\n"
                             "a3-mixer firmware|rev v03.0|GPL-3.0\n"
                             "a3-motion|v03.0+855|GPL-3.0\na3-motion pcb|rev v03|CERN-OHL-S\na3-motion housing||CERN-OHL-S|no version\n"
                             "a3-motion firmware|rev v03.0+2|GPL-3.0\na3-motion/ui|v03.0+900|GPL-3.0\nstemdeck||"));
    // Every row fits on the card at the rig size (768x1024): the tree ends above the link line.
    view.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    int linksTop = view.getHeight();
    for (auto* child : view.getChildren())
        if (dynamic_cast<juce::HyperlinkButton*> (child) != nullptr && child->getY() > view.getHeight() / 2)
            linksTop = juce::jmin (linksTop, child->getY());
    EXPECT_LE (view.treeBottom(), linksTop) << "tree bottom " << view.treeBottom() << " links " << linksTop;
    view.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    a3apptest::expectPainted (a3apptest::snapshotAt (view, "system3-about-tree", view.getWidth(), view.getHeight()));
    repo.deleteRecursively();
}

TEST_F (SystemPageTest, AboutShowsTheProjectSupportAndThePins)
{
    auto runner = rigRunner();
    a3app::SystemProbe probe (fakeSources (runner), false);
    a3app::SystemAboutView view (probe);
    view.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    EXPECT_FALSE (view.checkoutShown().found);
    probe.pollNow();
    view.refresh();
    EXPECT_EQ (view.checkoutShown().version, "v03.0+12");
    juce::StringArray linkTexts;
    for (auto* child : view.getChildren())
        if (auto* link = dynamic_cast<juce::HyperlinkButton*> (child))
            linkTexts.add (link->getButtonText());
    EXPECT_EQ (linkTexts, (juce::StringArray { "Documentation", "Website", "Source", juce::String::fromUTF8 ("Support A³"), "a3-core", "stemdeck" }))
        << "the bottom links on one line, then PUBLISHED: each repository a link";
    juce::StringArray headings;
    for (auto* child : view.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
            headings.add (label->getText());
    EXPECT_TRUE (headings.contains ("PUBLISHED")) << headings.joinIntoString (", ");
    // One card, PUBLISHED: its heading, the system line, a line per repository, the licences; no empty line.
    const auto t = a3app::layout::tokens (view);
    const int line = view.lineHeight();
    EXPECT_LT (line, t.row) << "compact, as STATUS' driver lines";
    const auto& cards = view.cardAreas();
    ASSERT_EQ (cards.size(), 1u);
    EXPECT_EQ (cards[0].getHeight(), 2 * t.padding + t.heading + (1 + 2) * line) << "PUBLISHED: the system line and two repositories";
    EXPECT_GE (cards[0].getY(), view.getHeight() / 3) << "below the top third, which is the logo's";
    // The links sit at the bottom, one line, centred; the logo's room is the top third.
    int linkY = -1;
    for (auto* child : view.getChildren())
        if (auto* link = dynamic_cast<juce::HyperlinkButton*> (child); link != nullptr && link->getButtonText() == "Documentation")
            linkY = link->getY();
    EXPECT_GT (linkY, cards[0].getBottom()) << "the links are under the card";
    EXPECT_GT (linkY, view.getHeight() * 2 / 3);
    a3apptest::expectPainted (a3apptest::snapshotAt (view, "system3-about", view.getWidth(), view.getHeight()));
}
