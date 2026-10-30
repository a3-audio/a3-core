// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <vector>

#include "CoreClient.h"
#include "Look.h"

namespace a3app
{
// DEPARTMENTS: who besides the mixer and Motion is told every A3 message (a light desk, a video desk).
// On top the service file's --subscriber entries, read-only; below them the list Core keeps in its own
// config, one row per entry (name, host, port, on, REMOVE), ADD and SAVE LIST under it. Every entry is
// checked as it is typed (a name, unique and not Core's own; an IPv4 address or a host name; a port 1-65535)
// and SAVE LIST stays off while one is wrong; whether a host name resolves, Core decides when it is saved.
// Core applies a saved list at once, without a restart. Each row says what Core does with it: sending (how many messages,
// the last how long ago), quiet, off, not saved, or that the service file's entry of that name wins.
class DepartmentsPage final : public juce::Component, public CardPage
{
public:
    // `askEveryMillis`: how often the page asks Core for its report while it is shown.
    explicit DepartmentsPage (CoreClient&, int askEveryMillis = 1000);
    ~DepartmentsPage() override;

    // Asks Core (at most every askEveryMillis) and takes its report when one came; an edit not saved yet is
    // never overwritten by it.
    void refresh();

    // The stored list as edited.
    int entryCount() const;
    a3core::Department entry (int index) const;   // as typed; a port that is no number reads 0
    void addEntry();
    void removeEntry (int index);
    void setEntry (int index, const a3core::Department&);
    void setEntryText (int index, const juce::String& name, const juce::String& host, const juce::String& port);
    juce::String problemOf (int index) const;   // "" when the entry can be saved
    bool hasUnsaved() const;
    bool canSave() const;
    // Sends the whole list to Core; refused here first when an entry is wrong, then by Core (a host that
    // does not resolve, a file it cannot write).
    juce::Result saveNow();

    juce::String statusOf (const juce::String& name) const;   // a stored entry's status
    juce::StringArray serviceNames() const;
    juce::String serviceStatusOf (const juce::String& name) const;
    juce::String answerText() const { return answer.getText(); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class EntryRow;
    class ServiceRow;

    void takeReport (const DepartmentsReport&);
    void loadEntries (const std::vector<a3core::Department>&);
    void edited();
    void showStatuses();
    void layoutRows();
    std::vector<a3core::Department> edits() const;
    const DepartmentRow* reported (const std::string& name, const std::string& from) const;
    int indexOf (const EntryRow*) const;

    CoreClient& client;
    const int askEvery;
    double askedAt = -1.0e9;
    std::uint64_t seenGeneration = 0;
    DepartmentsReport report;
    std::vector<a3core::Department> baseline;   // what Core keeps, as far as this page knows

    juce::Label title, nameCaption, hostCaption, portCaption, onCaption, statusCaption, answer;
    juce::Component list;
    juce::Viewport viewport;
    std::vector<std::unique_ptr<ServiceRow>> serviceRows;
    std::vector<std::unique_ptr<EntryRow>> entryRows;
    juce::TextButton addButton { "ADD" }, saveButton { "SAVE LIST" };   // not "Save": that is the engine's, in the footer
};
}
