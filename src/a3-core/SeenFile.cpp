// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SeenFile.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "Json.h"

namespace a3core
{
namespace
{
// Bumped when the shape of the saved rows changes: a file from another version restores nothing rather
// than nonsense.
constexpr int version = 1;

// A saved row in order: direction, address, count, peer, last_seen. A list rather than an object per
// row: at the unknown table's cap that is 3.29 MB instead of 5.34 MB and 62 ms instead of 93.
constexpr int rowWidth = 5, unknownWidth = 4;

class SystemClock : public Clock
{
public:
    double monotonic() override { return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count(); }
    double wall() override { return std::chrono::duration<double> (std::chrono::system_clock::now().time_since_epoch()).count(); }
};

}

Clock& systemClock()
{
    static SystemClock clock;
    return clock;
}

namespace
{

std::string utf8 (const juce::var& value) { return value.toString().toStdString(); }

bool isInteger (const juce::var& value) { return value.isInt() || value.isInt64() || value.isBool(); }
bool isNumber (const juce::var& value) { return isInteger (value) || value.isDouble(); }

// Python's int: a bool is one.
long long integerOf (const juce::var& value)
{
    return value.isBool() ? (static_cast<bool> (value) ? 1 : 0) : static_cast<juce::int64> (value);
}

// The rows of a saved list that can be read, skipping the ones that cannot: one unreadable row out of
// nineteen thousand must not cost the other nineteen thousand. Every shape a file can arrive in is a
// shape it may arrive in -- written by a version with a field more or fewer, truncated, hand-edited.
std::vector<juce::var> usable (const juce::var& rows, int width, int addressAt)
{
    std::vector<juce::var> good;
    const auto* list = rows.getArray();
    if (list == nullptr)
        return good;
    for (const auto& row : *list)
    {
        const auto* fields = row.getArray();
        if (fields == nullptr || fields->size() != width)
            continue;
        if (! row[addressAt].isString() || row[addressAt].toString().isEmpty())
            continue;
        if (! isInteger (row[addressAt + 1]) || ! isNumber (row[width - 1]))
            continue;
        if (! row[width - 2].isString())   // the peer: Python takes any value, a name is all it ever was
            continue;
        good.push_back (row);
    }
    return good;
}

std::optional<Direction> directionOf (const juce::var& value)
{
    if (! value.isString())
        return std::nullopt;
    if (value.toString() == "in")
        return Direction::in;
    if (value.toString() == "out")
        return Direction::out;
    return std::nullopt;
}

std::optional<juce::var> readSaved (const juce::File& path)
{
    juce::MemoryBlock bytes;
    if (! path.loadFileAsData (bytes))
        return std::nullopt;
    const auto* chars = static_cast<const char*> (bytes.getData());
    const auto size = static_cast<int> (bytes.getSize());
    if (! juce::CharPointer_UTF8::isValidString (chars, size))
        return std::nullopt;

    juce::String error;
    auto saved = parseJson (juce::String::fromUTF8 (chars, size), error);
    if (error.isNotEmpty() || saved.getDynamicObject() == nullptr || ! saved.getDynamicObject()->hasProperty ("version"))
        return std::nullopt;
    if (! pythonEquals (saved["version"], juce::var (version)))
        return std::nullopt;
    return saved;
}
}

juce::File stateDirectory (const std::optional<std::string>& xdgStateHome, const juce::File& home)
{
    if (xdgStateHome)
        return juce::File (juce::String::fromUTF8 (xdgStateHome->c_str()));
    return home.getChildFile (".local/state");
}

juce::File stateDirectory()
{
    const char* xdg = std::getenv ("XDG_STATE_HOME");
    return stateDirectory (xdg != nullptr ? std::optional<std::string> (xdg) : std::nullopt,
                           juce::File::getSpecialLocation (juce::File::userHomeDirectory));
}

juce::File seenPath (const juce::File& stateDir) { return stateDir.getChildFile ("a3-core/seen.json"); }

SeenFile::SeenFile (juce::File pathToUse, Traffic& trafficToUse, double delayToUse, Clock* clockToUse)
    : path (std::move (pathToUse)), traffic (trafficToUse), delay (delayToUse),
      clock (clockToUse != nullptr ? clockToUse : &systemClock())
{
}

int SeenFile::restore()
{
    const auto saved = readSaved (path);
    if (! saved)
        return 0;

    std::vector<RestoredRow> rows;
    for (const auto& item : usable ((*saved)["rows"], rowWidth, 1))
    {
        const auto direction = directionOf (item[0]);
        if (! direction)
            continue;
        rows.push_back ({ *direction, utf8 (item[1]), integerOf (item[2]), utf8 (item[3]), *numberOf (item[4]) });
    }

    std::vector<RestoredUnknown> unknown;
    for (const auto& item : usable ((*saved)["unknown"], unknownWidth, 0))
        unknown.push_back ({ utf8 (item[0]), integerOf (item[1]), utf8 (item[2]), *numberOf (item[3]) });

    return traffic.restore (rows, unknown, clock->wall(), clock->monotonic());
}

void SeenFile::write()
{
    // Read before the snapshots: a row made in between then reads as a list that changed since this
    // write, and is written next time, instead of being counted as written when it is not in the file.
    const auto created = traffic.created();
    const auto mono = clock->monotonic();
    // A cutoff of now: the history is no business of the file, and copying it is a megabyte and a half.
    const auto snapshot = traffic.snapshot (mono, mono);
    const auto unknown = traffic.unknownSnapshot (mono);

    // Both snapshots stamp an `at` from the monotonic clock; the file carries wall-clock times, so each
    // row's age is measured against the snapshot it came from and then taken from now.
    const auto now = clock->wall();
    const auto stamp = [now] (double at, double lastSeen) { return now - std::max (0.0, at - lastSeen); };

    juce::Array<juce::var> rows, unknownRows;
    for (const auto& row : snapshot.rows)
        rows.add (juce::var (juce::Array<juce::var> { juce::var (directionName (row.direction)), text (row.address),
                                                      juce::var (static_cast<juce::int64> (row.count)), text (row.peer),
                                                      juce::var (stamp (snapshot.at, row.lastSeen)) }));
    for (const auto& row : unknown.rows)
        unknownRows.add (juce::var (juce::Array<juce::var> { text (row.address), juce::var (static_cast<juce::int64> (row.count)),
                                                              text (row.peer), juce::var (stamp (unknown.at, row.lastSeen)) }));

    auto* object = new juce::DynamicObject();
    juce::var file (object);
    object->setProperty ("version", version);
    object->setProperty ("at", now);
    object->setProperty ("rows", juce::var (rows));
    object->setProperty ("unknown", juce::var (unknownRows));
    const auto contents = dumpPython (file);   // json.dumps(saved), byte for byte

    const auto cannot = [&] { problem = "cannot write " + path.getFullPathName().toStdString(); };
    if (! path.getParentDirectory().createDirectory())
        return cannot();
    // Atomic within a filesystem, so a crash mid-write leaves the previous file whole.
    const auto temporary = path.getSiblingFile (path.getFileName() + ".new");
    if (! temporary.replaceWithData (contents.data(), contents.size()))
        return cannot();
    std::error_code failure;
    std::filesystem::rename (temporary.getFullPathName().toStdString(), path.getFullPathName().toStdString(), failure);
    if (failure)
    {
        temporary.deleteFile();
        return cannot();
    }

    problem.clear();
    ++writes;
    writtenAtCreated = created;
}

void SeenFile::writeIfDue (double now)
{
    if (! nextLook)
    {
        nextLook = now + delay;
        return;
    }
    if (now < *nextLook)
        return;
    nextLook = now + delay;

    const auto created = traffic.created();
    // Two conditions, and both are needed: the list has to have changed since the last write, and it has
    // to have stopped changing since the last look -- otherwise REAPER's nineteen thousand addresses
    // would be written once per look all the way through the burst instead of once at the end of it.
    const bool settled = polled && *polled == created;
    polled = created;
    if (settled && (! writtenAtCreated || *writtenAtCreated != created))
        write();
}
}
