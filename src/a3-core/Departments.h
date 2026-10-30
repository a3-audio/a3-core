// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Departments: a light or video desk (anything beyond the mixer and Motion) that is told every A3
// message, as a --subscriber is. The window keeps the list in Core's own config
// (<configDir>/core/departments.json); Core subscribes the entries that are on, live. An entry is
// checked before it is stored, and a file edited by hand is read entry by entry: a bad department is
// left out and said, it never stops Core (an unparsable --subscriber does, on purpose).
//
// A name in both the service file (--subscriber) and the stored list: the command line wins, the
// stored entry is kept but not subscribed, and the window says so.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <juce_core/juce_core.h>

#include "Net.h"

namespace a3core
{
struct Department
{
    std::string name, host;
    int port = 0;
    bool on = true;

    bool operator== (const Department& other) const
    {
        return name == other.name && host == other.host && port == other.port && on == other.on;
    }
};

// Whether a host name resolves to an IPv4 address now (the service gives the system's resolver).
using HostResolves = std::function<bool (const std::string& host)>;

// The names Core already gives its own clients: the two that ship, the engine, its own port.
extern const std::vector<std::string> namesCoreKeeps;

// Why one field cannot be stored, or nullopt. The host is checked for its form only: a dotted IPv4
// address, or a host name (letters, digits, hyphens, dots).
std::optional<std::string> nameProblem (const std::string& name);
std::optional<std::string> hostProblem (const std::string& host);
std::optional<std::string> portProblem (int port);

// The first reason the whole list cannot be stored: an entry's field, a name used twice, a host name
// that does not resolve. Off entries are checked too: what is stored can be switched on.
std::optional<std::string> listProblem (const std::vector<Department>& list, const HostResolves& resolves);

// [{"name", "host", "port", "on"}, ...], and back (the shape only, or why it is not that).
juce::var departmentsJson (const std::vector<Department>& list);
std::variant<std::vector<Department>, std::string> departmentsFrom (const juce::var& array);

// <configDir>/core/departments.json, {"departments": [...]}.
juce::File departmentsFile (const juce::File& configDir);

struct StoredDepartments
{
    std::vector<Department> list;
    std::vector<std::string> problems;   // what was left out, and why: one line each
};

// Never throws: no file is an empty list; a broken file or entry is a problem and left out.
StoredDepartments loadDepartments (const juce::File& file);
// Atomically (a temporary file renamed over it); false when it could not be written.
bool saveDepartments (const juce::File& file, const std::vector<Department>& list);

// The clients Core subscribes: the entries that are on, except a name the service file already gives.
std::vector<Client> departmentClients (const std::vector<Department>& stored, const std::vector<std::string>& serviceNames);
}
