// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What Core tells the window about itself: B1's engine_status (the window's /api/status) and the rig
// (project 2's RIG STATE page: who is there). Both are built on the serve thread and published as text
// for the HTTP threads (CoreView); a call from any other thread is the mistake P8 forbids and throws.
#include "Core.h"

#include <algorithm>

#include "Json.h"

namespace a3core
{
namespace
{
juce::var textOrNull (const std::optional<std::string>& value) { return value ? text (*value) : juce::var(); }

}

juce::var Core::statusJson() const
{
    subscribersOwner.check ("the status");
    return objectOf ({ { "audio_engine", "engine" }, { "reason", "" } });
}

// engine.open means "Core sent the engine `open` since its last new hello": heard is about the
// hello, open about the recall after the replay, so a new boot id reads heard:true, open:false until
// that recall has opened it.
// Everything is seconds since the last word, not a clock reading: the page has no use for Core's
// monotonic clock. Null when there has been none.
juce::var Core::rigJson() const
{
    subscribersOwner.check ("the rig");
    const auto now = env.clock.monotonic();
    const auto since = [now] (const std::optional<double>& heard) { return heard ? juce::var (now - *heard) : juce::var(); };

    juce::Array<juce::var> deskRows;
    for (const auto& desk : desks.rows())
        deskRows.add (objectOf ({ { "host", text (desk.host) },
                                  { "kind", text (desk.kind) },
                                  { "name", text (deskName (desk.host, desk.kind)) },
                                  { "last_heard", since (desk.lastHeard) } }));

    juce::Array<juce::var> deckRows;
    for (int deck = 1; deck <= decks.decks(); ++deck)
    {
        const auto host = deckHosts.find (deck);
        const auto channel = decks.channelOf (deck);
        deckRows.add (objectOf ({ { "deck", deck },
                                  { "present", decks.present (deck, now) },
                                  { "host", host != deckHosts.end() ? text (host->second) : juce::var() },
                                  { "port", deckPort (truth, deck) },
                                  { "channel", channel ? juce::var (*channel) : juce::var() },
                                  { "last_heard", since (decks.lastHeardOf (deck)) } }));
    }

    return objectOf ({ { "motion", objectOf ({ { "followed", textOrNull (motion.followed()) },
                                               { "remote", motion.remote() },
                                               { "last_heard", since (motion.lastHeard()) } }) },
                       { "desks", deskRows },
                       { "decks", deckRows },
                       { "stemdeck", objectOf ({ { "host", textOrNull (stemdeckWatch.host()) } }) },
                       { "prodj_live", prodjLive.on() },
                       { "tempo_source", text (tempo.source()) },
                       { "engine", objectOf ({ { "heard", engineHeard() },
                                               { "boot_id", project ? textOrNull (project->lastBootId()) : juce::var() },
                                               { "open", engineOpen } }) },
                       { "devices", deviceTable.snapshot (now) } });
}

// The service file's first (read-only in the window), then the stored ones. A stored entry the service
// file overrides is not what was sent to that name: it reports nothing sent.
juce::var Core::departmentsJson() const
{
    subscribersOwner.check ("the departments");
    const auto now = env.clock.monotonic();
    const auto names = serviceNames();
    juce::Array<juce::var> rows;
    const auto rowOf = [&] (const std::string& name, const std::string& host, int port, bool on, const char* from, bool overridden) {
        const bool subscribed = on && ! overridden;
        const auto tally = sentTally.find (name);
        const bool counted = ! overridden && tally != sentTally.end();
        rows.add (objectOf ({ { "name", text (name) },
                              { "host", text (host) },
                              { "port", port },
                              { "on", on },
                              { "from", from },
                              { "subscribed", subscribed },
                              { "overridden", overridden },
                              { "sent", counted ? juce::var (tally->second.count) : juce::var (0) },
                              { "last_sent", counted ? juce::var (now - tally->second.last) : juce::var() } }));
    };
    for (const auto& subscriber : serviceSubscribers)
        rowOf (subscriber.name, subscriber.host, subscriber.port, true, "service", false);
    for (const auto& entry : storedDepartments)
        rowOf (entry.name, entry.host, entry.port, entry.on, "window",
               std::find (names.begin(), names.end(), entry.name) != names.end());
    return objectOf ({ { "departments", rows } });
}

void Core::publishView()
{
    subscribersOwner.check ("publishing the view");
    published.publish ({ canonicalBytes (statusJson()), canonicalBytes (rigJson()), canonicalBytes (departmentsJson()) });
    viewPublishedAt = env.clock.monotonic();
}

void Core::sendFromBench (const std::string& to, const OscMessage& message)
{
    subscribersOwner.check ("a bench send");
    if (to == "mixer")
        send (mixerClient, message);
    else if (to == "motion")
        send (motionClient, message);
    else
        throw std::invalid_argument ("the bench sends from Core to mixer and motion only, not to " + to);
}
}
