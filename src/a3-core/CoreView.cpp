// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What Core tells the window about itself: B1's engine_status (the window's /api/status) and the rig
// (project 2's RIG STATE page: who is there). Both are built on the serve thread and published as text
// for the HTTP threads (CoreView); a call from any other thread is the mistake P8 forbids and throws.
#include "Core.h"

#include "Json.h"

namespace a3core
{
namespace
{
juce::var text (const std::string& utf8) { return juce::var (juce::String::fromUTF8 (utf8.c_str(), static_cast<int> (utf8.size()))); }

juce::var textOrNull (const std::optional<std::string>& value) { return value ? text (*value) : juce::var(); }

juce::var objectOf (std::initializer_list<std::pair<const char*, juce::var>> members)
{
    auto* made = new juce::DynamicObject();
    for (const auto& member : members)
        made->setProperty (juce::Identifier (member.first), member.second);
    return juce::var (made);
}
}

juce::var Core::statusJson() const
{
    subscribersOwner.check ("the status");
    return objectOf ({ { "audio_engine", "engine" }, { "reason", "" } });
}

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
                       { "engine", objectOf ({ { "heard", engineHeard() },
                                               { "boot_id", project ? textOrNull (project->lastBootId()) : juce::var() },
                                               { "open", engineOpen } }) },
                       { "devices", deviceTable.snapshot (now) } });
}

void Core::publishView()
{
    subscribersOwner.check ("publishing the view");
    published.publish ({ canonicalBytes (statusJson()), canonicalBytes (rigJson()) });
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
