// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The RIG page's rig (the Canvas's tiles, a3-system canvas/Source/model, moved into the Engine window on
// 2026-10-30): each program of the rig, what systemd says of its units here, what Core hears and what each device
// reports of itself (/api/rig's devices and versions, /api/status' version), and the row the page writes from them.
// Pure: what is read is handed in, nothing here asks the machine.
#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace a3app::rig
{
enum class Kind { engine, deck, mixer, motion, analyzer };

// One program of the rig: the systemd user units that are it on its machine. The a3-core row is the engine and Core;
// the window that shows the page (a3-core-window.service) is not on it, so no key here ends the page itself.
struct Program
{
    Kind kind = Kind::engine;
    juce::String label;
    std::vector<juce::String> units;
    int deck = 0;   // 1-based for a deck, 0 otherwise
};

// Engine, a deck per slot (A … Z, AA, …), Mixer, Motion, Beat analyzer.
std::vector<Program> programs (int decks);

enum class Light { missing, stopped, starting, running, degraded, failed, elsewhere, unknown };

// What `systemctl [--user] show` says about one unit (fileState: UnitFileState, "enabled", "disabled", "static", ...).
struct UnitFacts
{
    juce::String load, active, sub, result, fileState;
};
std::vector<std::string> showCommand (const std::vector<Program>&);

// A unit of the Core on this machine, the user's or the system's (read-only: the window never acts on a system unit).
struct ServiceUnit
{
    juce::String name;
    bool system = false;
    bool operator== (const ServiceUnit& other) const { return name == other.name && system == other.system; }
};
// The packages that make the Core (a3-core, the deployment; a3-core-app, the program): their unit files.
std::vector<std::string> packageUnitsCommand();
// `dpkg-query -L` output: the .service and .timer files under a systemd user or system directory (not the .wants links).
std::vector<ServiceUnit> packageUnits (const juce::String& dpkgList);
std::vector<std::string> mainWantsCommand();                  // systemctl --user show -p Wants --value a3-main.service
std::vector<juce::String> parseWants (const juce::String& output);
// The Core's services (2026-10-31, the maintainer: the a3-core card lists every unit that belongs to the Core here): the
// packages' units and what a3-main.service Wants, less the units that are another row's (the decks, the desk, Motion).
std::vector<ServiceUnit> coreServices (const std::vector<ServiceUnit>& packaged, const std::vector<juce::String>& mainWants,
                                       const std::vector<Program>&);
std::vector<std::string> showCommand (const std::vector<ServiceUnit>&, bool system);
// Blocks split on blank lines, each named by its Id=. A block without an Id is dropped.
std::map<juce::String, UnitFacts> parseSystemctlShow (const juce::String& text);

// One device's report of itself (device.version), as /api/rig carries it; empty where it said none.
struct Report
{
    juce::String device, host, version, firmware, audio;
};

// Core's GET /api/rig and GET /api/status: who Core hears and what each device reports.
struct CoreFacts
{
    bool answers = false;
    juce::String coreVersion;                 // /api/status' version: the package's ("dev" for a build none made)
    std::set<int> presentDecks;               // 1-based; Core hears its hello and the engine its audio
    std::map<int, juce::String> deckHosts;    // who sent each deck's hello
    juce::String deskHost, motionHost;        // the first desk Core hears; the Motion Core follows
    std::map<juce::String, double> heard;     // device (as /api/rig names it) -> seconds since its last hello
    std::vector<Report> reports;              // /api/rig's versions: every device.version Core keeps
};
// An empty or unparsable body is "Core does not answer", never an error.
CoreFacts parseCore (const juce::String& rigBody, const juce::String& statusBody);

// The truth's network (a3-osc.json: every machine by name, the control and audio networks as address/prefix) and this
// machine's own addresses.
struct Network
{
    std::map<juce::String, juce::String> hosts;
    juce::String control, audio;
    std::vector<juce::String> own;
};
struct InterfaceAddress
{
    juce::String interface, address;
};
Network truthNetwork (const juce::var& truthData);
bool inSubnet (const juce::String& address, const juce::String& cidr);
// This machine's addresses: on the control network, then the audio network, then any other; never loopback,
// link-local or a container or VM bridge.
std::vector<juce::String> ownAddresses (const std::vector<InterfaceAddress>&, const Network&);
// This machine's IPv4 addresses with their interfaces' names (getifaddrs).
std::vector<InterfaceAddress> listInterfaces();


// The row the page writes for one program.
// One line of the a3-core card: a service, its state ("active · enabled") and the light for it.
struct ServiceLine
{
    juce::String name, state;
    Light light = Light::unknown;
};

struct Row
{
    Light light = Light::unknown;
    juce::String state;      // "Running", "Stopped", "Heard by Core", "a3-core failed (exit-code)", ...
    juce::String where;      // "local · 192.168.8.10", "192.168.8.13"; empty: nobody names it
    juce::String audio;      // its address on the audio network; empty: none known
    juce::String software;   // its version, or "not reported"
    juce::String firmware;   // empty for a program without a board (only the Mixer's Pico and Motion's panel)
    bool here = false;       // runs on this machine: its keys act here
    bool installed = false;  // here, and systemd knows its units (the desk: its program is installed)
    bool up = false;         // something of it runs or tries to: its one key is Stop
    std::vector<ServiceLine> services;   // the a3-core row's: every unit of the Core here
    bool insideCore = false;             // a program whose unit is one of the Core's (the beat analyzer): no row of its own
};

// Everything the rows are written from.
struct Facts
{
    std::map<juce::String, UnitFacts> units;
    CoreFacts core;
    Network network;
    bool deskInstalled = false;   // /usr/bin/a3-desk: the desk has no unit file, it runs as a transient one
    std::vector<ServiceUnit> coreServices;          // what the a3-core card lists
    std::map<juce::String, UnitFacts> systemUnits;  // `systemctl show` of the Core's system units
};

Row describe (const Program&, const Facts&);
inline const juce::String notReported = "not reported";
}
