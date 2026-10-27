// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from B1's a3-core.py (a3-core 7a7b2441f74e); the function each method comes from is named
// beside it. Engine mode only: REAPER's send points (osc_reaper, the IEM clients, the DualDelay)
// reach nobody there, so their sends are not ported (O8).
#include "Core.h"

#include <algorithm>
#include <stdexcept>

#include "CoreState.h"
#include "Evening.h"
#include "OscWire.h"
#include "StemsWords.h"

namespace a3core
{
namespace
{
std::set<std::string> ownHostsOf (const Truth& truth, const std::string& motionHost)
{
    // MOTION_OWN_HOSTS, with --motion's host added as the main block does.
    const auto address = truth.network()["address"].toString().upToFirstOccurrenceOf ("/", false, false);
    return { truth.host ("core"), address.toStdString(), motionHost };
}

int channelCountOf (const Truth& truth) { return truth.indexRange ("channel.gain", "ch").second; }
int deckCountOf (const Truth& truth) { return truth.indexRange ("stemdeck.deck-hello", "deck").second; }
}

bool isPosition (const Truth& truth, const std::string& address)
{
    const auto found = truth.match (address);
    return found && (found->first == "channel.azimuth" || found->first == "channel.elevation");
}

Core::Core (CoreEnvironment environment, CoreArgs coreArgs)
    : env (std::move (environment)), args (std::move (coreArgs)), truth (env.truth),
      statePath (env.stateDir.getChildFile ("a3-core/state.json")),
      eveningPath (env.stateDir.getChildFile ("a3-core/evening.json")),
      seenFilePath (seenPath (env.stateDir)),
      deviceTable (truth.fingerprint()),
      seenFile (seenFilePath, trafficTable, SeenFile::defaultDelay, &env.clock),
      sendFailures ([this] (const std::string& line, bool toStderr) { say (line, toStderr); }),
      mixerClient { "mixer", args.mixer }, motionClient { "motion", args.motion },
      engineClient { "engine", truth.endpoint ("engine", "osc") },
      motionOwnHosts (ownHostsOf (truth, args.motion.host)),
      motion (args.motion, truth.port ("motion", "osc"), motionOwnHosts, truth.port ("motion", "vu")),
      desks (truth.port ("desk", "osc"), truth.port ("desk", "vu")),
      saidKinds (truth.port ("desk", "osc")),
      channels (static_cast<size_t> (channelCountOf (truth))),
      stateFile (statePath), eveningFile (eveningPath),
      startupRecall (env.newToken()),
      decks (channelCountOf (truth), deckCountOf (truth)),
      stemdeckWatch (stemdeckSilence, [this] (const std::string& host) { return isThisMachine (host, motionOwnHosts); }, true)
{
    // What B1 does at import, before the main block: the state file read back into the channels and
    // the stem mirror (apply_state, apply_stems).
    const auto state = stateFile.load();
    applyState (state, channels, master);
    stems = Stems::fromData (state.isObject() ? state["stems"] : juce::var());
    mapHandlers();
}

// The main block's engine-mode start-up, the part that is Core's (a3-core.py:2240-2614): the
// service says `audio engine: engine`, binds the sockets, opens the window and announces (Task 20).
void Core::start()
{
    peerHosts = { { "mixer", args.mixer.host }, { "motion", args.motion.host },
                  { "reaper", truth.endpoint ("reaper", "osc").host } };
    // The desk is in front: its values count from its first message, hello or not.
    helloGate.trustHost (peerHosts["mixer"]);

    subscribers = { mixerClient, motionClient };
    auto reserved = shipped;
    reserved.insert (reserved.end(), { "reaper", "iem", "dualdelay" });
    for (const auto& extra : parseSubscribers (args.subscribers, reserved))   // SubscriberError: the service refuses to start
    {
        subscribers.push_back ({ extra.name, { extra.host, extra.port } });
        peerHosts[extra.name] = extra.host;
        say ("subscriber " + extra.name + " at " + extra.host + ":" + std::to_string (extra.port));
    }

    // Replayed on the engine's hello; after the first replay the evening is Core's own live one.
    project.emplace (replayable (truth, eveningFile.load()),
                     [this] { return replayable (truth, eveningState (relayed)); });   // the_live_evening
    decks.fromEvening (eveningFile.load());

    speakRememberedState();

    say (std::to_string (seenFile.restore()) + " addresses remembered from " + seenFilePath.getFullPathName().toStdString());
    seenFile.writeIfDue (env.clock.monotonic());   // _seen_file.follow(): the first look is a delay from now

    helloGate.trust (env.selfSocket);
    subscribersOwner.claim();
}

void Core::handlePacket (const Endpoint& from, const uint8_t* data, size_t size)
{
    const auto messages = decodePacket (data, size);
    if (! messages)
        return;   // python-osc's ParseError: dropped without a word
    for (const auto& message : *messages)
        dispatcher.dispatch (from, message);
}

void Core::handleVu (const Endpoint&, const uint8_t*, size_t)
{
    notPortedYet ("a3_core_vu_relay.forward and vu_destinations (Tasks 19, 22)");
}

// on_tick, then what B1's timers and SeenFile's thread did: the debounced writes that fell due.
void Core::tick()
{
    // tidy_when_settled is REAPER mode's stem mirror: nothing in engine mode.
    noticeMotionSilence (env.clock.monotonic());
    noticeDeskSilence (env.clock.monotonic());
    noticeDeckPresence (env.clock.monotonic());
    if (prodjLive.due (env.clock.monotonic()))
        announceLive();

    const auto now = env.clock.monotonic();
    flushIfDue (stateFile, statePath, now);
    flushIfDue (eveningFile, eveningPath, now);
    const auto before = seenFile.writes;
    seenFile.writeIfDue (now);
    tellWritten (before, seenFile.writes, seenFilePath);
}

// The SIGTERM handler's stop(): what is still inside a delay goes to disk now.
void Core::stop()
{
    for (auto* file : { &stateFile, &eveningFile })
    {
        const auto before = file->writes();
        file->flush();
        tellWritten (before, file->writes(), file == &stateFile ? statePath : eveningPath);
    }
    const auto before = seenFile.writes;
    seenFile.write();
    tellWritten (before, seenFile.writes, seenFilePath);
}

juce::var Core::statusJson() const
{
    notPortedYet ("engine_status (Task 21)");
}

//==============================================================================
// The doors out.

// WatchedClient.send_message: the traffic row, then the send, a failure said once.
void Core::send (const Client& client, const OscMessage& message)
{
    // A message of several arguments is a Python list; Traffic holds one value per row (Task 21).
    const OscArg* value = message.args.size() == 1 ? &message.args.front() : nullptr;
    trafficTable.seen (Direction::out, message.address, value, client.name, env.clock.monotonic());
    sendFailures.send (client.name, [&] { env.out.send (client, message); });
}

// send_message(address, value): a value of None is a message without arguments.
void Core::send (const Client& client, const Message& message)
{
    OscMessage out { message.first, {} };
    if (message.second.tag != 'N')
        out.args.push_back (message.second);
    send (client, out);
}

void Core::toEverySubscriber (const OscMessage& message)
{
    for (const auto& client : subscribers)
        send (client, message);
}

void Core::broadcast (const OscMessage& message)
{
    if (message.args.size() != 1)
        throw std::logic_error ("broadcast " + message.address + ": Relayed holds one value, not a list (Task 18)");
    const auto& value = message.args.front();
    if (relayed.holds (message.address, value))
        return;
    notePassedOn (message.address, value);
    const auto told = subscribers;   // tuple(subscribers): a send may not change who is told
    for (const auto& client : told)
        send (client, message);
}

void Core::notePassedOn (const std::string& address, const OscArg& value)
{
    relayed.note (address, value);
    if (project && project->live)
        writeTheEvening();
}

void Core::writeTheEvening()
{
    notPortedYet ("write_the_evening (Task 18)");
}

bool Core::engineHeard() const { return project && project->round > 0; }

void Core::sendToEngine (const std::vector<Message>& messages)
{
    if (! engineHeard())
        return;   // Core asks, devices answer: nothing before the engine's hello
    if (! messages.empty())
        notPortedYet ("send_to_engine after the hello (Task 18)");
}

void Core::say (const std::string& line, bool toStderr) { env.journal (line, toStderr); }

//==============================================================================
// What is said whole.

void Core::speakRememberedState()
{
    // REAPER's half (remembered_reaper_messages, send_cue_levels) reaches nobody in engine mode.
    const auto messages = recallMessages (truth, channels, master, relayed);
    for (const auto& message : messages)
        for (const auto& client : subscribers)
            send (client, message);
    speakStems (true);
    say ("startup: spoke the remembered state, " + std::to_string (messages.size()) + " messages to "
         + std::to_string (subscribers.size()) + " subscribers");
}

int Core::sayTheWholeState()
{
    notPortedYet ("say_the_whole_state (Task 19)");
}

void Core::speakStems (bool full)
{
    // REAPER's half (analog, stems and return-source sends) reaches nobody in engine mode.
    for (const auto& announcement : announcements (stems, truth))
    {
        if (full)
            toEverySubscriber (announcement);
        else
            broadcast (announcement);
    }
    if (full)
        sourcesSentToEngine.clear();
    // Marked as told even before the engine's hello: changed_messages runs whether send_to_engine
    // sends or not, as in B1.
    sendToEngine (changedMessages (sourceMessages (truth, decks, engineReturnSource (stems.returnMode)), sourcesSentToEngine));
    reportDecks (full);
}

void Core::reportDecks (bool full)
{
    const auto now = env.clock.monotonic();
    std::vector<Message> messages;
    for (int channel = 1; channel <= decks.channels(); ++channel)
        messages.emplace_back (truth.address ("channel.source", { { "ch", channel } }), pyInt (decks.source (channel)));
    for (int channel = 1; channel <= decks.channels(); ++channel)
        messages.emplace_back (truth.address ("channel.source.cursor", { { "ch", channel } }),
                               pyInt (decks.ring (channel)[static_cast<size_t> (decks.cursor (channel))]));
    for (int deck = 1; deck <= decks.decks(); ++deck)
        messages.emplace_back (truth.address ("stemdeck.present", { { "deck", deck } }), pyInt (decks.present (deck, now) ? 1 : 0));
    for (const auto& [address, value] : messages)
    {
        const OscMessage message { address, { value } };
        if (full)
            toEverySubscriber (message);
        else
            broadcast (message);
    }
}

//==============================================================================
// The tick's watchers.

void Core::noticeMotionSilence (double now)
{
    if (const auto moved = motion.silence (now))
        pointMotionAt (*moved, "the remote one is silent, back to the rig's");
}

void Core::pointMotionAt (const Endpoint&, const std::string&)
{
    notPortedYet ("point_motion_at (Task 19)");
}

void Core::noticeDeskSilence (double now)
{
    subscribersOwner.check ("a desk leaving");
    if (! desks.silence (now).empty())
        notPortedYet ("notice_desk_silence's leaving desks (Task 19)");
}

bool Core::deskLeaves (const std::string&)
{
    notPortedYet ("desk_leaves (Task 19)");
}

void Core::liveOffWhenNoProdjIsLeft()
{
    notPortedYet ("live_off_when_no_prodj_is_left (Task 19)");
}

void Core::announceLive()
{
    notPortedYet ("announce_live (Task 19)");
}

std::vector<Client> Core::deckListeners() const
{
    notPortedYet ("deck_listeners (Task 19)");
}

void Core::noticeDeckPresence (double now)
{
    constexpr double everySeconds = 1.0;   // DECK_PRESENCE_EVERY_SECONDS
    if (now - decksLookedAt < everySeconds)
        return;
    decksLookedAt = now;
    reportDecks (false);
}

void Core::flushIfDue (StateFile& file, const juce::File& path, double now)
{
    const auto before = file.writes();
    file.flushIfDue (now);
    tellWritten (before, file.writes(), path);
}

void Core::tellWritten (int before, int after, const juce::File& path)
{
    if (after != before && env.fileWritten)
        env.fileWritten (path);
}


//==============================================================================
// The dispatcher.

void Core::mapHandlers()
{
    const std::vector<std::pair<const char*, const char*>> families {
        { "channel", "osc_handler_channel (Task 18)" }, { "master", "osc_handler_master (Task 18)" },
        { "filter", "osc_handler_filter (Task 18)" },   { "aux-return", "osc_handler_aux_return (Task 18)" },
        { "stemdeck", "osc_handler_stemdeck (Task 19)" } };
    for (const auto& [family, handler] : families)
        dispatcher.map (std::string ("/") + family + "/*", [this, handler = handler] (const Endpoint&, const OscMessage&) {
            notPortedYet (handler);
        });
    const std::vector<std::pair<std::string, const char*>> words {
        { truth.address ("state.recall"), "osc_handler_recall (Task 19)" },
        { truth.address ("beat"), "osc_handler_beat (Task 19)" },
        { truth.address ("device.hello"), "osc_handler_device_hello (Task 19)" },
        { truth.address ("engine.hello"), "osc_handler_engine_hello (Task 19)" } };
    for (const auto& [address, handler] : words)
        dispatcher.map (address, [this, handler = handler] (const Endpoint&, const OscMessage&) { notPortedYet (handler); });
    const auto [firstDeck, lastDeck] = truth.indexRange ("stemdeck.deck-hello", "deck");
    for (int deck = firstDeck; deck <= lastDeck; ++deck)
        dispatcher.map (truth.address ("stemdeck.deck-hello", { { "deck", deck } }),
                        [this] (const Endpoint&, const OscMessage&) { notPortedYet ("osc_handler_deck_hello (Task 19)"); });
    dispatcher.map (truth.address ("prodj.live"),
                    [this] (const Endpoint&, const OscMessage&) { notPortedYet ("osc_handler_prodj_live (Task 19)"); });
    for (const auto* pointer : { "stemdeck.pointer", "motion.pointer" })
        dispatcher.map (truth.address (pointer),
                        [this] (const Endpoint&, const OscMessage&) { notPortedYet ("osc_handler_pointer (Task 19)"); });
    dispatcher.setDefault ([this] (const Endpoint& from, const OscMessage& message) { unrouted (from, message); });
}

void Core::unrouted (const Endpoint&, const OscMessage&)
{
    notPortedYet ("unrouted_handler (Task 18)");
}

void Core::notPortedYet (const char* handler) const
{
    throw std::logic_error (std::string ("a3core::Core: ") + handler + " is not ported yet");
}
}
