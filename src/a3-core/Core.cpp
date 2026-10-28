// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from B1's a3-core.py (a3-core 7a7b2441f74e); the function each method comes from is named
// beside it. Engine mode only: B1's REAPER send points (osc_reaper, the IEM clients, the DualDelay)
// were never ported (O8), and REAPER left the Core on 2026-10-27.
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
      engineClient { "engine", truth.endpoint ("engine", "osc") }, selfClient { "self", { truth.host ("local"), args.port } },
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
    startPeers();
    startSpeaking();
}

void Core::startPeers()
{
    peerHosts = { { "mixer", args.mixer.host }, { "motion", args.motion.host } };
    // The desk is in front: its values count from its first message, hello or not.
    helloGate.trustHost (peerHosts["mixer"]);

    subscribers = { mixerClient, motionClient };
    for (const auto& extra : parseSubscribers (args.subscribers, shipped))   // SubscriberError: the service refuses to start
    {
        subscribers.push_back ({ extra.name, { extra.host, extra.port } });
        peerHosts[extra.name] = extra.host;
        say ("subscriber " + extra.name + " at " + extra.host + ":" + std::to_string (extra.port));
    }

    // Replayed on the engine's hello; after the first replay the evening is Core's own live one.
    project.emplace (replayable (truth, eveningFile.load()),
                     [this] { return replayable (truth, eveningState (relayed)); });   // the_live_evening
    decks.fromEvening (eveningFile.load());
    if (! truth.hasAddress ("engine.deck-audio"))
        say ("the truth has no engine.deck-audio word: a deck's presence rests on its hellos alone, not on what the engine hears", true);
}

// Called on the serve thread: what follows is handled there, and the claim says so.
void Core::startSpeaking()
{
    speakRememberedState();

    say (std::to_string (seenFile.restore()) + " addresses remembered from " + seenFilePath.getFullPathName().toStdString());
    seenFile.writeIfDue (env.clock.monotonic());   // _seen_file.follow(): the first look is a delay from now

    helloGate.trust (env.selfSocket);
    subscribersOwner.claim();
    publishView();   // the window has something to show before the first tick
}

void Core::serveOn (int port)
{
    args.port = port;
    selfClient.to.port = port;
}

void Core::handlePacket (const Endpoint& from, const uint8_t* data, size_t size)
{
    const auto messages = decodePacket (data, size);
    if (! messages)
        return;   // python-osc's ParseError: dropped without a word
    for (const auto& message : *messages)
        dispatcher.dispatch (from, message);
}

// on_tick, then what B1's timers and SeenFile's thread did: the debounced writes that fell due.
void Core::tick()
{
    // B1's tidy_when_settled mirrored the stems into REAPER: not ported (O8).
    noticeMotionSilence (env.clock.monotonic());
    noticeDeskSilence (env.clock.monotonic());
    noticeDeckPresence (env.clock.monotonic());
    lookAtTheTempo (env.clock.monotonic());
    if (prodjLive.due (env.clock.monotonic()))
        announceLive();

    const auto now = env.clock.monotonic();
    flushIfDue (stateFile, statePath, now);
    flushIfDue (eveningFile, eveningPath, now);
    const auto before = seenFile.writes;
    seenFile.writeIfDue (now);
    tellWritten (before, seenFile.writes, seenFilePath);

    if (! viewPublishedAt || now - *viewPublishedAt >= publishEvery)
        publishView();
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


//==============================================================================
// The doors out.

// WatchedClient.send_message: the traffic row, then the send, a failure said once.
void Core::send (const Client& client, const OscMessage& message)
{
    // A message of several arguments is a Python list.
    if (message.args.size() > 1)
        trafficTable.seenList (Direction::out, message.address, message.args, client.name, env.clock.monotonic());
    else
        trafficTable.seen (Direction::out, message.address, message.args.empty() ? nullptr : &message.args.front(), client.name,
                           env.clock.monotonic());
    sendFailures.send (client.name, [&] { env.out.send (client, message); });
}

namespace
{
// send_message(address, value): a value of None is a message without arguments.
OscMessage messageOf (const std::string& address, const OscArg& value)
{
    OscMessage out { address, {} };
    if (value.tag != 'N')
        out.args.push_back (value);
    return out;
}
}

void Core::send (const Client& client, const Message& message) { send (client, messageOf (message.first, message.second)); }

// self_client: Core's own port, from the one socket the hello gate trusts. A raw client in B1, so
// nothing in the traffic table and no send failure said.
void Core::sendToSelf (const std::string& address, const OscArg& value) { env.out.send (selfClient, messageOf (address, value)); }

void Core::toEverySubscriber (const OscMessage& message)
{
    for (const auto& client : subscribers)
        send (client, message);
}

// A message of one argument passes on that value; one of several is a Python list (the return's
// word), held and written as one.
void Core::broadcast (const OscMessage& message)
{
    const bool list = message.args.size() != 1;
    if (list ? relayed.holds (message.address, message.args) : relayed.holds (message.address, message.args.front()))
        return;
    notePassedOn (message.address, message.args);
    const auto told = subscribers;   // tuple(subscribers): a send may not change who is told
    for (const auto& client : told)
        send (client, message);
}

void Core::notePassedOn (const std::string& address, const std::vector<OscArg>& value)
{
    if (value.size() == 1)
        relayed.note (address, value.front());
    else
        relayed.note (address, value);
    if (project && project->live)
        writeTheEvening();
}

// Written along, so a power cut does not cost the evening; the decks' places beside its values.
void Core::writeTheEvening()
{
    auto state = eveningState (relayed);
    const auto places = decks.toEvening();
    for (const auto& entry : places.getDynamicObject()->getProperties())
        state.getDynamicObject()->setProperty (entry.name, entry.value);
    eveningFile.remember (state, env.clock.monotonic());
}

bool Core::engineHeard() const { return project && project->round > 0; }

void Core::sendToEngine (const std::vector<Message>& messages)
{
    if (! engineHeard())
        return;   // Core asks, devices answer: nothing before the engine's hello
    for (const auto& message : messages)
        send (engineClient, message);
}

void Core::say (const std::string& line, bool toStderr) { env.journal (line, toStderr); }

//==============================================================================
// What is said whole.

void Core::speakRememberedState()
{
    // B1's REAPER half (remembered_reaper_messages, send_cue_levels) is not ported (O8).
    const auto messages = recallMessages (truth, channels, master, relayed);
    for (const auto& message : messages)
        for (const auto& client : subscribers)
            send (client, message);
    speakStems (true);
    say ("startup: spoke the remembered state, " + std::to_string (messages.size()) + " messages to "
         + std::to_string (subscribers.size()) + " subscribers");
}

void Core::speakStems (bool full)
{
    // B1's REAPER half (analog, stems and return-source sends) is not ported (O8).
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
    sendToEngine (changedMessages (sourceMessages (truth, decks), sourcesSentToEngine));
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

// Once a second: a beat clock that stopped hands over to the decks with no message arriving.
void Core::lookAtTheTempo (double now)
{
    constexpr double everySeconds = 1.0;
    if (now - tempoLookedAt < everySeconds)
        return;
    tempoLookedAt = now;
    followDeckTempo();
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
    using Method = void (Core::*) (const Endpoint&, const OscMessage&);
    const auto to = [this] (Method method) {
        return [this, method] (const Endpoint& from, const OscMessage& message) { (this->*method) (from, message); };
    };
    const std::vector<std::pair<const char*, Method>> families {
        { "channel", &Core::channelHandler }, { "master", &Core::masterHandler }, { "filter", &Core::filterHandler },
        { "aux-return", &Core::auxReturnHandler }, { "stemdeck", &Core::stemdeckHandler } };
    for (const auto& [family, method] : families)
        dispatcher.map (std::string ("/") + family + "/*", to (method));
    dispatcher.map (truth.address ("state.recall"), to (&Core::recall));
    dispatcher.map (truth.address ("beat"), to (&Core::beatHandler));
    dispatcher.map (truth.address ("device.hello"), to (&Core::deviceHello));
    dispatcher.map (truth.address ("engine.hello"), to (&Core::engineHello));
    const auto [firstDeck, lastDeck] = truth.indexRange ("stemdeck.deck-hello", "deck");
    for (int deck = firstDeck; deck <= lastDeck; ++deck)
        dispatcher.map (truth.address ("stemdeck.deck-hello", { { "deck", deck } }), to (&Core::deckHello));
    // A truth from before the engine reported its decks' audio (the recorded B0 one) has no such word.
    if (truth.hasAddress ("engine.deck-audio"))
        for (int deck = firstDeck; deck <= lastDeck; ++deck)
            dispatcher.map (truth.address ("engine.deck-audio", { { "deck", deck } }), to (&Core::engineDeckAudio));
    // Nor did it know the decks' tempo; without the word the delay follows the beat clock alone.
    if (truth.hasAddress ("stemdeck.tempo"))
    {
        const auto [firstReporting, lastReporting] = truth.indexRange ("stemdeck.tempo", "deck");
        for (int deck = firstReporting; deck <= lastReporting; ++deck)
            dispatcher.map (truth.address ("stemdeck.tempo", { { "deck", deck } }), to (&Core::deckTempo));
    }
    dispatcher.map (truth.address ("prodj.live"), to (&Core::prodjLiveHandler));
    for (const auto* pointer : { "stemdeck.pointer", "motion.pointer" })
        dispatcher.map (truth.address (pointer), to (&Core::pointerHandler));
    dispatcher.setDefault ([this] (const Endpoint& from, const OscMessage& message) { unrouted (from, message); });
}

// unrouted_handler: what reaches the main port and no map -- written down, nothing done.
void Core::unrouted (const Endpoint& from, const OscMessage& message)
{
    trafficTable.unknown (message.address, message.args.empty() ? nullptr : &message.args.front(),
                          peerName (from.host, peerHosts, commanders), env.clock.monotonic());
}

void Core::notPortedYet (const char* handler) const
{
    throw std::logic_error (std::string ("a3core::Core: ") + handler + " is not ported yet");
}
}
