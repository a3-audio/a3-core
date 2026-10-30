// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Departments.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "Json.h"

namespace a3core
{
namespace
{
constexpr int lowestPort = 1, highestPort = 65535;
constexpr size_t longestHostName = 253, longestLabel = 63;

bool isDigit (char c) { return std::isdigit (static_cast<unsigned char> (c)) != 0; }
bool isLabelCharacter (char c) { return std::isalnum (static_cast<unsigned char> (c)) != 0 || c == '-'; }

std::vector<std::string> split (const std::string& text, char separator)
{
    std::vector<std::string> pieces;
    size_t from = 0;
    for (auto at = text.find (separator); at != std::string::npos; at = text.find (separator, from))
    {
        pieces.push_back (text.substr (from, at - from));
        from = at + 1;
    }
    pieces.push_back (text.substr (from));
    return pieces;
}

bool isDottedQuad (const std::string& host)
{
    const auto parts = split (host, '.');
    if (parts.size() != 4)
        return false;
    return std::all_of (parts.begin(), parts.end(), [] (const std::string& part) {
        return ! part.empty() && part.size() <= 3 && std::all_of (part.begin(), part.end(), isDigit) && std::stoi (part) <= 255;
    });
}

bool isLabel (const std::string& label)
{
    return ! label.empty() && label.size() <= longestLabel && label.front() != '-' && label.back() != '-'
           && std::all_of (label.begin(), label.end(), isLabelCharacter);
}

bool isHostName (const std::string& host)
{
    const auto labels = split (host, '.');
    return host.size() <= longestHostName && std::all_of (labels.begin(), labels.end(), isLabel);
}

bool looksNumeric (const std::string& host)
{
    return std::all_of (host.begin(), host.end(), [] (char c) { return isDigit (c) || c == '.'; });
}

std::optional<std::string> entryProblem (const Department& entry)
{
    const auto who = entry.name.empty() ? std::string ("an entry") : entry.name;
    if (const auto problem = nameProblem (entry.name))
        return who + ": " + *problem;
    if (const auto problem = hostProblem (entry.host))
        return who + ": " + *problem;
    if (const auto problem = portProblem (entry.port))
        return who + ": " + *problem;
    return std::nullopt;
}

// One entry's shape, or why it is not one.
std::variant<Department, std::string> entryFrom (const juce::var& item)
{
    if (! item.isObject())
        return std::string ("an entry is not an object");
    const auto& name = item["name"];
    const auto& host = item["host"];
    const auto& port = item["port"];
    const auto& on = item["on"];
    if (! name.isString() || ! host.isString())
        return std::string ("an entry needs a name and a host as text");
    if (! (port.isInt() || port.isInt64()))
        return name.toString().toStdString() + ": the port must be a whole number";
    if (! on.isBool())
        return name.toString().toStdString() + ": on must be true or false";
    return Department { name.toString().toStdString(), host.toString().toStdString(), static_cast<int> (port), static_cast<bool> (on) };
}
}

const std::vector<std::string> namesCoreKeeps { "mixer", "motion", "engine", "self" };

std::optional<std::string> nameProblem (const std::string& name)
{
    if (name.empty() || std::all_of (name.begin(), name.end(), [] (char c) { return std::isspace (static_cast<unsigned char> (c)) != 0; }))
        return std::string ("a name is needed");
    if (name.find_first_of ("@= \t") != std::string::npos)
        return "the name " + name + " may not hold a space, @ or =";
    if (std::find (namesCoreKeeps.begin(), namesCoreKeeps.end(), name) != namesCoreKeeps.end())
        return "the name " + name + " is Core's own";
    return std::nullopt;
}

std::optional<std::string> hostProblem (const std::string& host)
{
    if (host.empty())
        return std::string ("a host is needed");
    if (looksNumeric (host))
        return isDottedQuad (host) ? std::nullopt : std::optional<std::string> (host + " is not an IPv4 address");
    if (! isHostName (host))
        return host + " is neither an IPv4 address nor a host name";
    return std::nullopt;
}

std::optional<std::string> portProblem (int port)
{
    if (port < lowestPort || port > highestPort)
        return "the port must be " + std::to_string (lowestPort) + " to " + std::to_string (highestPort) + ", not " + std::to_string (port);
    return std::nullopt;
}

std::optional<std::string> listProblem (const std::vector<Department>& list, const HostResolves& resolves)
{
    std::set<std::string> names;
    for (const auto& entry : list)
    {
        if (const auto problem = entryProblem (entry))
            return problem;
        if (! names.insert (entry.name).second)
            return "two entries are called " + entry.name;
    }
    for (const auto& entry : list)
        if (! isDottedQuad (entry.host) && resolves && ! resolves (entry.host))
            return entry.name + ": the host " + entry.host + " does not resolve";
    return std::nullopt;
}

juce::var departmentsJson (const std::vector<Department>& list)
{
    juce::Array<juce::var> items;
    for (const auto& entry : list)
        items.add (objectOf ({ { "name", text (entry.name) }, { "host", text (entry.host) }, { "port", entry.port }, { "on", entry.on } }));
    return items;
}

std::variant<std::vector<Department>, std::string> departmentsFrom (const juce::var& array)
{
    if (! array.isArray())
        return std::string ("the departments must be a list");
    std::vector<Department> list;
    for (const auto& item : *array.getArray())
    {
        auto entry = entryFrom (item);
        if (const auto* problem = std::get_if<std::string> (&entry))
            return *problem;
        list.push_back (std::get<Department> (std::move (entry)));
    }
    return list;
}

juce::File departmentsFile (const juce::File& configDir) { return configDir.getChildFile ("core/departments.json"); }

StoredDepartments loadDepartments (const juce::File& file)
{
    StoredDepartments stored;
    if (! file.existsAsFile())
        return stored;
    juce::String error;
    const auto data = parseJson (file.loadFileAsString(), error);
    const auto& items = data["departments"];
    if (error.isNotEmpty() || ! items.isArray())
    {
        stored.problems.push_back (file.getFullPathName().toStdString() + " is not a departments file, no department is told");
        return stored;
    }
    std::set<std::string> names;
    for (const auto& item : *items.getArray())
    {
        auto read = entryFrom (item);
        if (const auto* problem = std::get_if<std::string> (&read))
        {
            stored.problems.push_back (*problem + ", left out");
            continue;
        }
        const auto entry = std::get<Department> (std::move (read));
        if (const auto problem = entryProblem (entry))
            stored.problems.push_back (*problem + ", left out");
        else if (! names.insert (entry.name).second)
            stored.problems.push_back ("a second " + entry.name + ", left out");
        else
            stored.list.push_back (entry);
    }
    return stored;
}

bool saveDepartments (const juce::File& file, const std::vector<Department>& list)
{
    if (! file.getParentDirectory().createDirectory())
        return false;
    // replaceWithText writes a temporary file beside it and moves it over: a crash leaves the old list.
    return file.replaceWithText (juce::String (dumpIndented (objectOf ({ { "departments", departmentsJson (list) } }), 2)) + "\n", false, false, "\n");
}

std::vector<Client> departmentClients (const std::vector<Department>& stored, const std::vector<std::string>& serviceNames)
{
    std::vector<Client> clients;
    for (const auto& entry : stored)
        if (entry.on && std::find (serviceNames.begin(), serviceNames.end(), entry.name) == serviceNames.end())
            clients.push_back ({ entry.name, { entry.host, entry.port } });
    return clients;
}
}
