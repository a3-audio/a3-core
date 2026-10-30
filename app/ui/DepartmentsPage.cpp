// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DepartmentsPage.h"

#include <algorithm>

#include "MonitorTable.h"

namespace a3app
{
namespace
{
const std::string fromService = "service", fromWindow = "window";

// The columns as shares of a row's width: name, host, port, on, REMOVE; the status takes the rest.
constexpr float nameShare = 0.16f, hostShare = 0.22f, portShare = 0.10f, onShare = 0.07f, removeShare = 0.12f;

struct Columns
{
    juce::Rectangle<int> name, host, port, on, remove, status;
};

Columns columnsOf (juce::Rectangle<int> row)
{
    const auto width = (float) row.getWidth();
    const int gap = juce::jmax (2, row.getHeight() / 8);
    Columns columns;
    const auto take = [&] (float share) {
        auto cell = row.removeFromLeft ((int) (width * share));
        return cell.withTrimmedRight (gap);
    };
    columns.name = take (nameShare);
    columns.host = take (hostShare);
    columns.port = take (portShare);
    columns.on = take (onShare);
    columns.remove = take (removeShare);
    columns.status = row;
    return columns;
}

// "12 sent, the last 0.4 s ago", or that nothing was sent.
juce::String sentText (const DepartmentRow& row)
{
    if (! row.lastSent)
        return "nothing sent yet";
    return juce::String (row.sent) + " sent, the last " + ageText (*row.lastSent) + " ago";
}

// What Core does with a row it subscribes: sending while messages are younger than the monitor's "active".
juce::String liveText (const DepartmentRow& row)
{
    if (row.lastSent && *row.lastSent < staleAfterSeconds)
        return "sending: " + sentText (row);
    return "quiet: " + sentText (row);
}

std::optional<int> portOf (const juce::String& typed)
{
    const auto trimmed = typed.trim();
    if (trimmed.isEmpty() || ! trimmed.containsOnly ("0123456789") || trimmed.length() > 6)
        return std::nullopt;
    return trimmed.getIntValue();
}
}

// One stored entry: three fields, the on switch, REMOVE and its status.
class DepartmentsPage::EntryRow final : public juce::Component
{
public:
    explicit EntryRow (DepartmentsPage& owner)
    {
        styleField (name, "light");
        styleField (host, "IPv4 or host name");
        styleField (port, "port");
        port.setInputRestrictions (6, "0123456789");
        on.setToggleState (true, juce::dontSendNotification);
        look::caption (status);
        for (auto* field : { &name, &host, &port })
            field->onTextChange = [&owner] { owner.edited(); };
        on.onClick = [&owner] { owner.edited(); };
        remove.onClick = [&owner, this] { owner.removeEntry (owner.indexOf (this)); };
        for (auto* child : std::initializer_list<juce::Component*> { &name, &host, &port, &on, &remove, &status })
            addAndMakeVisible (child);
    }

    void show (const a3core::Department& entry)
    {
        name.setText (utf8 (entry.name), false);
        host.setText (utf8 (entry.host), false);
        port.setText (entry.port > 0 ? juce::String (entry.port) : juce::String(), false);
        on.setToggleState (entry.on, juce::dontSendNotification);
    }

    void showText (const juce::String& nameText, const juce::String& hostText, const juce::String& portText)
    {
        name.setText (nameText, false);
        host.setText (hostText, false);
        port.setText (portText, false);
    }

    a3core::Department typed() const
    {
        return { name.getText().trim().toStdString(), host.getText().trim().toStdString(), portOf (port.getText()).value_or (0),
                 on.getToggleState() };
    }

    // The form alone: the name, the host's form, the port. A name used twice is the page's to say.
    juce::String problem() const
    {
        const auto entry = typed();
        if (const auto why = a3core::nameProblem (entry.name))
            return utf8 (*why);
        if (const auto why = a3core::hostProblem (entry.host))
            return utf8 (*why);
        if (! portOf (port.getText()))
            return "the port must be a whole number";
        if (const auto why = a3core::portProblem (entry.port))
            return utf8 (*why);
        return {};
    }

    void say (const juce::String& text, void (*tone) (juce::Label&))
    {
        tone (status);
        look::say (status, text);
    }

    juce::String statusText() const { return status.getText(); }

    void resized() override
    {
        const auto columns = columnsOf (getLocalBounds());
        name.setBounds (columns.name);
        host.setBounds (columns.host);
        port.setBounds (columns.port);
        on.setBounds (columns.on);
        remove.setBounds (columns.remove);
        status.setBounds (columns.status);
        for (auto* field : { &name, &host, &port })
            fitField (*field);
    }

private:
    juce::TextEditor name, host, port;
    juce::ToggleButton on;
    juce::TextButton remove { "REMOVE" };
    juce::Label status;
};

// One --subscriber of the service file: shown, never edited.
class DepartmentsPage::ServiceRow final : public juce::Component
{
public:
    ServiceRow()
    {
        for (auto* label : { &name, &host, &port, &on, &status })
        {
            look::caption (*label);
            addAndMakeVisible (*label);
        }
        look::say (on, "on");
    }

    void show (const DepartmentRow& row, const juce::String& statusText)
    {
        look::say (name, utf8 (row.entry.name));
        look::say (host, utf8 (row.entry.host));
        look::say (port, juce::String (row.entry.port));
        look::say (status, statusText);
        entryName = row.entry.name;
    }

    const std::string& shownName() const { return entryName; }
    juce::String statusText() const { return status.getText(); }

    void resized() override
    {
        const auto columns = columnsOf (getLocalBounds());
        name.setBounds (columns.name);
        host.setBounds (columns.host);
        port.setBounds (columns.port);
        on.setBounds (columns.on);
        status.setBounds (columns.status.withLeft (columns.remove.getX()));
    }

private:
    juce::Label name, host, port, on, status;
    std::string entryName;
};

DepartmentsPage::DepartmentsPage (CoreClient& c, int askEveryMillis) : client (c), askEvery (askEveryMillis)
{
    look::caption (title);
    look::say (title, "Departments: a light or video desk that is told every A3 message, as the mixer and Motion are. "
                      "SAVE LIST applies it at once, without a restart.");
    const std::pair<juce::Label*, const char*> captions[] { { &nameCaption, "NAME" }, { &hostCaption, "HOST" }, { &portCaption, "PORT" },
                                                            { &onCaption, "ON" }, { &statusCaption, "STATUS" } };
    for (const auto& [label, text] : captions)
    {
        look::caption (*label);
        look::say (*label, text);
        addAndMakeVisible (*label);
    }
    look::caption (answer);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addButton.onClick = [this] { addEntry(); };
    saveButton.onClick = [this] { saveNow(); };
    for (auto* child : std::initializer_list<juce::Component*> { &title, &viewport, &addButton, &saveButton, &answer })
        addAndMakeVisible (child);
    edited();
}

DepartmentsPage::~DepartmentsPage() = default;

void DepartmentsPage::refresh()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - askedAt >= askEvery)
    {
        client.requestDepartments();
        askedAt = now;
    }
    const auto generation = client.departmentsGeneration();
    if (generation == seenGeneration)
        return;
    seenGeneration = generation;
    takeReport (client.departments());
}

void DepartmentsPage::takeReport (const DepartmentsReport& taken)
{
    report = taken;
    if (! report.answered)
    {
        look::failure (answer);
        look::say (answer, "no Core at " + client.url() + ": the list cannot be read or saved");
    }
    else if (answer.getText().startsWith ("no Core"))
        look::say (answer, {});

    std::vector<a3core::Department> stored;
    std::vector<const DepartmentRow*> service;
    for (const auto& row : report.rows)
    {
        if (row.from == fromWindow)
            stored.push_back (row.entry);
        else if (row.from == fromService)
            service.push_back (&row);
    }
    if (report.answered && ! hasUnsaved())
    {
        baseline = stored;
        if (edits() != stored)
            loadEntries (stored);
    }
    if (serviceRows.size() != service.size())
    {
        serviceRows.clear();
        for (size_t i = 0; i < service.size(); ++i)
        {
            serviceRows.push_back (std::make_unique<ServiceRow>());
            list.addAndMakeVisible (*serviceRows.back());
        }
        layoutRows();
    }
    showStatuses();
}

void DepartmentsPage::loadEntries (const std::vector<a3core::Department>& entries)
{
    entryRows.clear();
    for (const auto& entry : entries)
    {
        entryRows.push_back (std::make_unique<EntryRow> (*this));
        entryRows.back()->show (entry);
        list.addAndMakeVisible (*entryRows.back());
    }
    layoutRows();
    edited();
}

int DepartmentsPage::entryCount() const { return (int) entryRows.size(); }

a3core::Department DepartmentsPage::entry (int index) const { return entryRows.at ((size_t) index)->typed(); }

void DepartmentsPage::addEntry()
{
    entryRows.push_back (std::make_unique<EntryRow> (*this));
    list.addAndMakeVisible (*entryRows.back());
    layoutRows();
    edited();
}

void DepartmentsPage::removeEntry (int index)
{
    if (index < 0 || index >= entryCount())
        return;
    entryRows.erase (entryRows.begin() + index);
    layoutRows();
    edited();
}

void DepartmentsPage::setEntry (int index, const a3core::Department& entry)
{
    entryRows.at ((size_t) index)->show (entry);
    edited();
}

void DepartmentsPage::setEntryText (int index, const juce::String& name, const juce::String& host, const juce::String& port)
{
    entryRows.at ((size_t) index)->showText (name, host, port);
    edited();
}

juce::String DepartmentsPage::problemOf (int index) const
{
    const auto own = entryRows.at ((size_t) index)->problem();
    if (own.isNotEmpty())
        return own;
    const auto name = entry (index).name;
    for (int earlier = 0; earlier < index; ++earlier)
        if (entry (earlier).name == name)
            return "two entries are called " + utf8 (name);
    return {};
}

bool DepartmentsPage::hasUnsaved() const { return edits() != baseline; }

bool DepartmentsPage::canSave() const
{
    for (int i = 0; i < entryCount(); ++i)
        if (problemOf (i).isNotEmpty())
            return false;
    return hasUnsaved();
}

juce::Result DepartmentsPage::saveNow()
{
    for (int i = 0; i < entryCount(); ++i)
    {
        const auto problem = problemOf (i);
        if (problem.isEmpty())
            continue;
        look::failure (answer);
        look::say (answer, "not saved: " + problem);
        return juce::Result::fail (problem);
    }
    const auto list = edits();
    const auto result = client.saveDepartments (list);
    if (result.failed())
    {
        look::failure (answer);
        look::say (answer, "refused: " + result.getErrorMessage());
        return result;
    }
    baseline = list;
    look::value (answer);
    look::say (answer, "saved: Core applies it now");
    edited();
    return result;
}

std::vector<a3core::Department> DepartmentsPage::edits() const
{
    std::vector<a3core::Department> typed;
    for (const auto& row : entryRows)
        typed.push_back (row->typed());
    return typed;
}

const DepartmentRow* DepartmentsPage::reported (const std::string& name, const std::string& from) const
{
    for (const auto& row : report.rows)
        if (row.entry.name == name && row.from == from)
            return &row;
    return nullptr;
}

int DepartmentsPage::indexOf (const EntryRow* row) const
{
    for (size_t i = 0; i < entryRows.size(); ++i)
        if (entryRows[i].get() == row)
            return (int) i;
    return -1;
}

void DepartmentsPage::edited()
{
    showStatuses();
    saveButton.setEnabled (canSave());
}

void DepartmentsPage::showStatuses()
{
    for (int i = 0; i < entryCount(); ++i)
    {
        auto& row = *entryRows[(size_t) i];
        const auto problem = problemOf (i);
        if (problem.isNotEmpty())
        {
            row.say (problem, look::failure);
            continue;
        }
        const auto typed = row.typed();
        const auto* live = reported (typed.name, fromWindow);
        if (live == nullptr || ! (live->entry == typed))
            row.say ("not saved", look::caption);
        else if (live->overridden)
            row.say ("not used: the service file's " + utf8 (typed.name) + " wins", look::warning);
        else if (! typed.on)
            row.say ("off: told nothing", look::caption);
        else
            row.say (liveText (*live), look::value);
    }
    const auto service = [this] {
        std::vector<const DepartmentRow*> rows;
        for (const auto& row : report.rows)
            if (row.from == fromService)
                rows.push_back (&row);
        return rows;
    }();
    for (size_t i = 0; i < serviceRows.size() && i < service.size(); ++i)
    {
        auto text = juce::String ("service file, read-only; ") + liveText (*service[i]);
        if (reported (service[i]->entry.name, fromWindow) != nullptr)
            text << "; also in the list below, this one is used";
        serviceRows[i]->show (*service[i], text);
    }
}

juce::String DepartmentsPage::statusOf (const juce::String& name) const
{
    for (const auto& row : entryRows)
        if (utf8 (row->typed().name) == name)
            return row->statusText();
    return {};
}

juce::StringArray DepartmentsPage::serviceNames() const
{
    juce::StringArray names;
    for (const auto& row : serviceRows)
        names.add (utf8 (row->shownName()));
    return names;
}

juce::String DepartmentsPage::serviceStatusOf (const juce::String& name) const
{
    for (const auto& row : serviceRows)
        if (utf8 (row->shownName()) == name)
            return row->statusText();
    return {};
}

void DepartmentsPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    g.setColour (palette::raised);
    for (const auto& row : serviceRows)
        g.fillRect (viewport.getBounds().getIntersection (row->getBounds() + viewport.getPosition() - viewport.getViewPosition()));
}

void DepartmentsPage::layoutRows()
{
    const int row = look::rowHeight (*this);
    const int width = juce::jmax (1, viewport.getMaximumVisibleWidth());
    const int gap = juce::jmax (2, row / 10);
    int y = 0;
    for (auto& shown : serviceRows)
    {
        shown->setBounds (0, y, width, row - gap);
        y += row;
    }
    for (auto& shown : entryRows)
    {
        shown->setBounds (0, y, width, row - gap);
        y += row;
    }
    list.setSize (width, juce::jmax (y, 1));
    repaint();
}

void DepartmentsPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    const int row = look::rowHeight (*this);
    const int gap = row / 4;
    title.setBounds (area.removeFromTop (row * 3 / 2));
    title.setMinimumHorizontalScale (1.0f);
    auto captions = columnsOf (area.removeFromTop (row));
    nameCaption.setBounds (captions.name);
    hostCaption.setBounds (captions.host);
    portCaption.setBounds (captions.port);
    onCaption.setBounds (captions.on);
    statusCaption.setBounds (captions.status);
    auto bottom = area.removeFromBottom (row);
    area.removeFromBottom (gap);
    addButton.setBounds (bottom.removeFromLeft (getWidth() / 7));
    bottom.removeFromLeft (gap);
    saveButton.setBounds (bottom.removeFromLeft (getWidth() / 7));
    bottom.removeFromLeft (gap * 2);
    answer.setBounds (bottom);
    viewport.setBounds (area);
    layoutRows();
}
}
