// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "RigModel.h"
#include "EnginePorts.h"

#include <algorithm>
#include <functional>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>

namespace a3app::rig
{
namespace
{
// Core's thresholds for the monitor (CoreClient.h deadAfterSeconds): a device silent this long is not heard.
constexpr double silentAfterSeconds = 60.0;
// Interfaces that carry no A³ traffic: containers and virtual machines.
const juce::StringArray bridgePrefixes { "docker", "br-", "veth", "virbr", "lo" };
const juce::String dot = juce::String::fromUTF8 (" · ");

juce::String deckLetter (int deck) { return juce::String (a3app::deckLetters (deck)); }

// An IPv4 address as a number; -1 when it is not one.
juce::int64 ipv4 (const juce::String& text)
{
    const auto parts = juce::StringArray::fromTokens (text.trim(), ".", {});
    if (parts.size() != 4)
        return -1;
    juce::int64 value = 0;
    for (const auto& part : parts)
    {
        if (part.isEmpty() || ! part.containsOnly ("0123456789") || part.getIntValue() > 255)
            return -1;
        value = value * 256 + part.getIntValue();
    }
    return value;
}

// 127/8 and 169.254/16 by their leading bytes: no address literal for what is no A³ machine.
bool isLoopback (const juce::String& address) { return ipv4 (address) >= 0 && (ipv4 (address) >> 24) == 127; }
bool isLinkLocal (const juce::String& address) { return ipv4 (address) >= 0 && (ipv4 (address) >> 16) == (169 << 8 | 254); }

bool isBridge (const juce::String& interface)
{
    for (const auto& prefix : bridgePrefixes)
        if (interface.startsWith (prefix))
            return true;
    return false;
}

juce::String truthHost (const Network& network, const juce::String& name)
{
    const auto host = network.hosts.find (name);
    return host != network.hosts.end() ? host->second : juce::String();
}

// Deck 1 is "deckhost-a": the truth names each deck's machine by its letter.
juce::String deckHostName (int deck) { return "deckhost-" + deckLetter (deck).toLowerCase(); }

// Core's live word first, then the machine the truth names for the role.
juce::String addressOf (const Program& program, const Facts& facts)
{
    const auto& core = facts.core;
    switch (program.kind)
    {
        case Kind::engine:
        case Kind::analyzer: return truthHost (facts.network, "core");
        case Kind::deck:
        {
            const auto deck = core.deckHosts.find (program.deck);
            return deck != core.deckHosts.end() ? deck->second : truthHost (facts.network, deckHostName (program.deck));
        }
        case Kind::mixer: return core.deskHost.isNotEmpty() ? core.deskHost : truthHost (facts.network, "mixer");
        case Kind::motion: return core.motionHost.isNotEmpty() ? core.motionHost : truthHost (facts.network, "motion");
    }
    return {};
}

bool isOwn (const juce::String& address, const Network& network)
{
    return isLoopback (address) || std::find (network.own.begin(), network.own.end(), address) != network.own.end();
}

// "local" and this machine's address on the control network (else its first).
juce::String local (const Network& network)
{
    for (const auto& address : network.own)
        if (! inSubnet (address, network.audio))
            return "local" + dot + address;
    return "local";
}

// The machine's audio address as the truth names it: the host whose control address this is, and its "-audio" twin.
juce::String truthAudio (const juce::String& address, const Network& network)
{
    for (const auto& [name, value] : network.hosts)
        if (value == address)
            if (const auto audio = truthHost (network, name + "-audio"); audio.isNotEmpty())
                return audio;
    return {};
}

// The newest report of a device: from `host` when one is from there; any host's only where `anyHost`.
const Report* reportOf (const Facts& facts, const std::function<bool (const juce::String&)>& isDevice, const juce::String& host, bool anyHost)
{
    const Report* fallback = nullptr;
    for (const auto& report : facts.core.reports)
    {
        if (! isDevice (report.device))
            continue;
        if (report.host == host)
            return &report;
        if (fallback == nullptr && anyHost)
            fallback = &report;
    }
    return fallback;
}

bool named (const juce::String& device, const char* name) { return device == name; }

const Report* softwareReport (const Program& program, const Facts& facts, const juce::String& address)
{
    switch (program.kind)
    {
        case Kind::engine: return nullptr;
        case Kind::deck: return reportOf (facts, [] (const juce::String& d) { return named (d, "stemdeck"); }, address, false);
        case Kind::mixer: return reportOf (facts, [] (const juce::String& d) { return d.startsWith ("desk@"); }, address, true);
        case Kind::motion: return reportOf (facts, [] (const juce::String& d) { return named (d, "motion"); }, address, true);
        case Kind::analyzer: return reportOf (facts, [] (const juce::String& d) { return named (d, "beat-analyzer"); }, address, true);
    }
    return nullptr;
}

juce::String orNotReported (const juce::String& text) { return text.isNotEmpty() ? text : notReported; }

juce::String softwareOf (const Program& program, const Facts& facts, const Report* report)
{
    if (program.kind == Kind::engine)
        return orNotReported (facts.core.coreVersion);
    return orNotReported (report != nullptr ? report->version : juce::String());
}

// Only the Mixer's Pico (the mixer script reports it) and Motion's ESP32 panel have firmware.
juce::String firmwareOf (const Program& program, const Facts& facts, const Report* report)
{
    if (program.kind == Kind::motion)
        return orNotReported (report != nullptr ? report->firmware : juce::String());
    if (program.kind != Kind::mixer)
        return {};
    const auto* mixer = reportOf (facts, [] (const juce::String& d) { return named (d, "mixer"); }, {}, true);
    return orNotReported (mixer != nullptr ? mixer->firmware : juce::String());
}

UnitFacts unitOf (const Facts& facts, const juce::String& unit)
{
    const auto found = facts.units.find (unit);
    return found != facts.units.end() ? found->second : UnitFacts { "not-found", "inactive", "dead", "success" };
}

juce::String shortName (const juce::String& unit) { return unit.upToLastOccurrenceOf (".service", false, false); }
bool isStarting (const juce::String& active) { return active == "activating" || active == "reloading"; }

bool installedHere (const Program& program, const Facts& facts)
{
    if (program.kind == Kind::mixer)
        return facts.deskInstalled;
    for (const auto& unit : program.units)
        if (unitOf (facts, unit).load == "not-found")
            return false;
    return true;
}

// systemd's word on a program of this machine (the Canvas's TileStatus).
void stateHere (const Program& program, const Facts& facts, Row& row)
{
    if (facts.units.empty())
        return void (row.state = "not read yet");
    row.installed = installedHere (program, facts);
    if (! row.installed)
    {
        row.light = Light::missing;
        row.state = "Not installed";
        return;
    }
    int active = 0;
    bool starting = false;
    juce::String firstDown;
    for (const auto& unit : program.units)
    {
        const auto state = unitOf (facts, unit);
        if (state.active == "failed")
        {
            row.light = Light::failed;
            row.state = shortName (unit) + " failed (" + state.result + ")";
            return;
        }
        if (isStarting (state.active))
            starting = true;
        else if (state.active == "active")
            ++active;
        else if (firstDown.isEmpty())
            firstDown = unit;
    }
    if (starting)
        row.light = Light::starting, row.state = juce::String::fromUTF8 ("Starting…");
    else if (active == (int) program.units.size())
    {
        const bool unheardDeck = program.kind == Kind::deck && facts.core.answers && facts.core.presentDecks.count (program.deck) == 0;
        row.light = unheardDeck ? Light::degraded : Light::running;
        row.state = unheardDeck ? "Running, no audio link" : "Running";
    }
    else if (active > 0)
        row.light = Light::degraded, row.state = shortName (firstDown) + " not running";
    else
        row.light = Light::stopped, row.state = "Stopped";
}

bool heardRecently (const Facts& facts, const std::function<bool (const juce::String&)>& isDevice)
{
    for (const auto& [device, age] : facts.core.heard)
        if (isDevice (device) && age < silentAfterSeconds)
            return true;
    return false;
}

// What Core hears of a program on another machine: no unit of ours says anything about it.
void stateElsewhere (const Program& program, const Facts& facts, Row& row)
{
    if (! facts.core.answers)
        return void (row.state = "Core not answering");
    bool heard = false;
    switch (program.kind)
    {
        case Kind::deck: heard = facts.core.presentDecks.count (program.deck) > 0; break;
        case Kind::mixer: heard = heardRecently (facts, [] (const juce::String& d) { return d.startsWith ("desk@"); }); break;
        case Kind::motion: heard = heardRecently (facts, [] (const juce::String& d) { return d == "motion"; }); break;
        case Kind::engine:
        case Kind::analyzer: heard = false; break;
    }
    row.light = heard ? Light::running : Light::elsewhere;
    row.state = heard ? "Heard by Core" : "Not heard by Core";
}
}

std::vector<Program> programs (int decks)
{
    std::vector<Program> made;
    made.push_back ({ Kind::engine, "a3-core", { "a3-engine.service", "a3-core.service" } });
    for (int deck = 1; deck <= decks; ++deck)
        made.push_back ({ Kind::deck, "Deck " + deckLetter (deck), { "stemdeck@" + deckLetter (deck) + ".service" }, deck });
    made.push_back ({ Kind::mixer, juce::String::fromUTF8 ("A³ Mixer"), { "a3-desk.service" } });
    made.push_back ({ Kind::motion, juce::String::fromUTF8 ("A³ Motion"), { "a3-motion.service" } });
    made.push_back ({ Kind::analyzer, "Beat analyzer", { "beat-analyzer.service" } });
    return made;
}

namespace
{
const char* const showProperties = "Id,LoadState,ActiveState,SubState,Result,UnitFileState";
}

std::vector<std::string> showCommand (const std::vector<Program>& list)
{
    std::vector<std::string> argv { "systemctl", "--user", "show", "-p", showProperties };
    for (const auto& program : list)
        for (const auto& unit : program.units)
            argv.push_back (unit.toStdString());
    return argv;
}

std::vector<std::string> showCommand (const std::vector<ServiceUnit>& units, bool system)
{
    std::vector<std::string> argv { "systemctl" };
    if (! system)
        argv.push_back ("--user");
    for (const auto* word : { "show", "-p", showProperties })
        argv.push_back (word);
    for (const auto& unit : units)
        if (unit.system == system)
            argv.push_back (unit.name.toStdString());
    return argv;
}

std::vector<std::string> packageUnitsCommand() { return { "dpkg-query", "-L", "a3-core", "a3-core-app" }; }

std::vector<ServiceUnit> packageUnits (const juce::String& dpkgList)
{
    std::vector<ServiceUnit> units;
    for (const auto& line : juce::StringArray::fromLines (dpkgList))
    {
        const auto path = line.trim();
        if (! (path.endsWith (".service") || path.endsWith (".timer")) || path.contains (".wants/") || path.contains (".requires/"))
            continue;
        const bool user = path.contains ("/systemd/user/");
        const bool system = path.startsWith ("/etc/systemd/system/") || path.startsWith ("/lib/systemd/system/")
                            || path.startsWith ("/usr/lib/systemd/system/");
        if (! user && ! system)
            continue;
        ServiceUnit unit { path.fromLastOccurrenceOf ("/", false, false), system };
        if (std::find (units.begin(), units.end(), unit) == units.end())
            units.push_back (unit);
    }
    return units;
}

std::vector<std::string> mainWantsCommand() { return { "systemctl", "--user", "show", "-p", "Wants", "--value", "a3-main.service" }; }

std::vector<juce::String> parseWants (const juce::String& output)
{
    std::vector<juce::String> wants;
    for (const auto& word : juce::StringArray::fromTokens (output.trim(), " \t\n", ""))
        if (word.isNotEmpty())
            wants.push_back (word);
    return wants;
}

std::vector<ServiceUnit> coreServices (const std::vector<ServiceUnit>& packaged, const std::vector<juce::String>& mainWants,
                                       const std::vector<Program>& list)
{
    const auto anotherRows = [&list] (const juce::String& unit) {
        if (unit.startsWith ("stemdeck@") || unit.startsWith ("stemdeck."))
            return true;
        for (const auto& program : list)
            if (program.kind == Kind::deck || program.kind == Kind::mixer || program.kind == Kind::motion)
                if (std::find (program.units.begin(), program.units.end(), unit) != program.units.end())
                    return true;
        return false;
    };
    std::vector<ServiceUnit> users, systems;
    const auto add = [&] (const ServiceUnit& unit) {
        auto& into = unit.system ? systems : users;
        if (! anotherRows (unit.name) && std::find (into.begin(), into.end(), unit) == into.end())
            into.push_back (unit);
    };
    for (const auto& unit : packaged)
        add (unit);
    for (const auto& unit : mainWants)
        add ({ unit, false });
    const auto byName = [] (const ServiceUnit& a, const ServiceUnit& b) { return a.name < b.name; };
    std::sort (users.begin(), users.end(), byName);
    std::sort (systems.begin(), systems.end(), byName);
    users.insert (users.end(), systems.begin(), systems.end());
    return users;
}

std::map<juce::String, UnitFacts> parseSystemctlShow (const juce::String& text)
{
    std::map<juce::String, UnitFacts> units;
    UnitFacts unit;
    juce::String id;
    const auto finish = [&] {
        if (id.isNotEmpty())
            units[id] = unit;
        unit = {};
        id = {};
    };
    for (const auto& raw : juce::StringArray::fromLines (text))
    {
        const auto line = raw.trim();
        if (line.isEmpty())
        {
            finish();
            continue;
        }
        const auto key = line.upToFirstOccurrenceOf ("=", false, false), value = line.fromFirstOccurrenceOf ("=", false, false);
        if (key == "Id")
            id = value;
        else if (key == "LoadState")
            unit.load = value;
        else if (key == "ActiveState")
            unit.active = value;
        else if (key == "SubState")
            unit.sub = value;
        else if (key == "Result")
            unit.result = value;
        else if (key == "UnitFileState")
            unit.fileState = value;
    }
    finish();
    return units;
}

namespace
{
juce::var objectFrom (const juce::String& body)
{
    juce::var parsed;
    if (body.isEmpty() || juce::JSON::parse (body, parsed).failed() || parsed.getDynamicObject() == nullptr)
        return {};
    return parsed;
}

juce::String textOf (const juce::var& value) { return value.isString() ? value.toString() : juce::String(); }
}

CoreFacts parseCore (const juce::String& rigBody, const juce::String& statusBody)
{
    CoreFacts facts;
    const auto status = objectFrom (statusBody);
    const auto rig = objectFrom (rigBody);
    if (status.isVoid() && rig.isVoid())
        return facts;
    facts.answers = true;
    facts.coreVersion = textOf (status["version"]);
    if (const auto* decks = rig["decks"].getArray())
        for (const auto& deck : *decks)
        {
            const int number = static_cast<int> (deck["deck"]);
            if (number < 1)
                continue;
            if (static_cast<bool> (deck["present"]))
                facts.presentDecks.insert (number);
            if (textOf (deck["host"]).isNotEmpty())
                facts.deckHosts[number] = textOf (deck["host"]);
        }
    if (const auto* desks = rig["desks"].getArray())
        for (const auto& desk : *desks)
            if (textOf (desk["kind"]) == "desk" && textOf (desk["host"]).isNotEmpty())
            {
                facts.deskHost = textOf (desk["host"]);
                break;
            }
    facts.motionHost = textOf (rig["motion"]["followed"]);
    if (const auto* devices = rig["devices"].getArray())
        for (const auto& device : *devices)
            facts.heard[textOf (device["device"])] = static_cast<double> (device["age"]);
    if (const auto* versions = rig["versions"].getArray())
        for (const auto& report : *versions)
            facts.reports.push_back ({ textOf (report["device"]), textOf (report["host"]), textOf (report["version"]),
                                       textOf (report["firmware"]), textOf (report["audio"]) });
    return facts;
}

Network truthNetwork (const juce::var& truth)
{
    Network network;
    if (const auto* hosts = truth["hosts"].getDynamicObject())
        for (const auto& [name, value] : hosts->getProperties())
            network.hosts[name.toString()] = value.toString();
    network.control = textOf (truth["network"]["address"]);
    network.audio = textOf (truth["network"]["audio_address"]);
    return network;
}

bool inSubnet (const juce::String& address, const juce::String& cidr)
{
    const auto value = ipv4 (address);
    const auto base = ipv4 (cidr.upToFirstOccurrenceOf ("/", false, false));
    const auto bits = cidr.fromFirstOccurrenceOf ("/", false, false);
    if (value < 0 || base < 0 || bits.isEmpty() || ! bits.containsOnly ("0123456789") || bits.getIntValue() > 32)
        return false;
    const auto mask = bits.getIntValue() == 0 ? 0 : (0xffffffffLL << (32 - bits.getIntValue())) & 0xffffffffLL;
    return (value & mask) == (base & mask);
}

std::vector<juce::String> ownAddresses (const std::vector<InterfaceAddress>& interfaces, const Network& network)
{
    std::vector<juce::String> control, audio, other;
    for (const auto& [interface, address] : interfaces)
    {
        if (isBridge (interface) || ipv4 (address) < 0 || isLoopback (address) || isLinkLocal (address))
            continue;
        if (inSubnet (address, network.control))
            control.push_back (address);
        else if (inSubnet (address, network.audio))
            audio.push_back (address);
        else
            other.push_back (address);
    }
    control.insert (control.end(), audio.begin(), audio.end());
    control.insert (control.end(), other.begin(), other.end());
    return control;
}

// getifaddrs, not juce::IPAddress::getAllAddresses: only it names the interface, which is how a container bridge is
// told from the A³ network.
std::vector<InterfaceAddress> listInterfaces()
{
    std::vector<InterfaceAddress> found;
    ifaddrs* list = nullptr;
    if (getifaddrs (&list) != 0)
        return found;
    for (auto* entry = list; entry != nullptr; entry = entry->ifa_next)
    {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET)
            continue;
        char text[INET_ADDRSTRLEN] = {};
        const auto* address = reinterpret_cast<const sockaddr_in*> (entry->ifa_addr);
        if (inet_ntop (AF_INET, &address->sin_addr, text, sizeof (text)) != nullptr)
            found.push_back ({ entry->ifa_name, text });
    }
    freeifaddrs (list);
    return found;
}

namespace
{
Light serviceLight (const UnitFacts& unit)
{
    if (unit.active == "active")
        return Light::running;
    if (unit.active == "failed")
        return Light::failed;
    if (isStarting (unit.active) || unit.active == "deactivating")
        return Light::starting;
    return unit.load == "not-found" ? Light::missing : Light::stopped;
}

std::vector<ServiceLine> serviceLines (const Facts& facts)
{
    std::vector<ServiceLine> lines;
    for (const auto& service : facts.coreServices)
    {
        const auto& table = service.system ? facts.systemUnits : facts.units;
        const auto found = table.find (service.name);
        if (found == table.end())
        {
            lines.push_back ({ service.name, "not read yet", Light::unknown });
            continue;
        }
        const auto& unit = found->second;
        juce::String state = unit.load == "not-found" ? juce::String ("not installed") : unit.active;
        if (unit.fileState.isNotEmpty() && unit.load != "not-found")
            state << dot << unit.fileState;
        if (service.system)
            state << dot << "system";
        lines.push_back ({ service.name, state, serviceLight (unit) });
    }
    return lines;
}

bool coreHas (const Facts& facts, const std::vector<juce::String>& units)
{
    for (const auto& unit : units)
        for (const auto& service : facts.coreServices)
            if (! service.system && service.name == unit)
                return true;
    return false;
}
}

Row describe (const Program& program, const Facts& facts)
{
    Row row;
    const auto address = addressOf (program, facts);
    row.here = address.isEmpty() || isOwn (address, facts.network);
    row.where = address.isEmpty() ? juce::String() : row.here ? local (facts.network) : address;
    const auto* report = softwareReport (program, facts, address);
    row.software = softwareOf (program, facts, report);
    row.firmware = firmwareOf (program, facts, report);
    row.audio = report != nullptr && report->audio.isNotEmpty() ? report->audio : truthAudio (address, facts.network);
    if (row.here)
        stateHere (program, facts, row);
    else
        stateElsewhere (program, facts, row);
    row.up = row.here && (row.light == Light::running || row.light == Light::starting || row.light == Light::degraded);
    if (program.kind == Kind::engine)
        row.services = serviceLines (facts);
    row.insideCore = program.kind == Kind::analyzer && row.here && coreHas (facts, program.units);
    return row;
}
}
