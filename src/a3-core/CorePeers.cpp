// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Who Core follows and what it tells them, from B1's a3-core.py (a3-core 7a7b2441f74e), engine mode:
// the device and deck hellos, the one Motion and the one StemDeck followed, the desks and pro-DJ
// apps (four at most), LIVE and the pointers, the beat, StemDeck's preview, and the meters relayed
// to whoever the analyzer does not reach. The function each method comes from is named beside it.
// Everything here runs on the serve thread (P8); the subscriber list is changed only there.
#include "Core.h"

#include <algorithm>
#include <cmath>

#include "Ahead.h"
#include "CorePython.h"
#include "EngineWords.h"
#include "VuRelay.h"

namespace a3core
{
namespace
{
// A value as a handler received it, sent on by python-osc: the builder infers the tag again, so a
// small 64-bit int goes as 'i' and a double as 'f'.
OscArg sentOnAsPython (const OscArg& arg)
{
    switch (arg.tag)
    {
        case 'h': case 'r': return pyInt (std::get<int64_t> (arg.value));
        case 'd': return pyFloat (std::get<double> (arg.value));
        default: return arg;
    }
}

// list(osc_arguments), sent on.
std::vector<OscArg> sentOnAsPython (const std::vector<OscArg>& args)
{
    std::vector<OscArg> out;
    out.reserve (args.size());
    for (const auto& arg : args)
        out.push_back (sentOnAsPython (arg));
    return out;
}

// bool(int(raw)), as osc_handler_prodj_live reads LIVE: nullopt where B1 catches the TypeError or
// ValueError; int(inf) is an OverflowError B1 does not catch.
std::optional<bool> liveWanted (const OscArg* raw)
{
    if (raw == nullptr)
        return std::nullopt;
    if (const auto number = pythonFloat (*raw); (raw->tag == 'f' || raw->tag == 'd') && number && std::isinf (*number))
        throw PythonError ("OverflowError");
    if (const auto whole = pythonInt (*raw))
        return *whole != 0;
    return std::nullopt;
}

std::string joined (const std::vector<std::string>& parts, const std::string& separator)
{
    std::string out;
    for (const auto& part : parts)
        out += (out.empty() ? "" : separator) + part;
    return out;
}
}

//==============================================================================
// The hellos.

// osc_handler_device_hello: a device names itself and the sha256 of its truth.
void Core::deviceHello (const Endpoint& from, const OscMessage& message)
{
    seen (message, firstOrNone (message), originOf (from));
    if (message.args.size() < 2)
        return;
    const auto device = pythonStr (message.args[0]);
    const auto theirHash = pythonStr (message.args[1]);
    const bool matches = theirHash == deviceTable.ownHash;
    const auto now = env.clock.monotonic();
    saidKinds.said (from.host, device);
    if (device == "stemdeck")
        stemdeckSaidHello (from.host, now);
    if (device == "motion")
        motionSaidHello (from.host, now);
    if ((device == deskKind || device == prodjKind) && matches)
        deskSaidHello (from.host, now, device);
    helloGate.greet (from.host, matches);   // after Motion's hello: its whole state first, then its values
    noticeStemdeckSilence (now);
    const auto label = device == deskKind || device == prodjKind ? deskName (from.host, device) : device;
    if (deviceTable.heard (label, theirHash, now))
        say (label + ": its a3-osc.json " + (matches ? "is Core's" : "DIFFERS from Core's") + " (" + firstCharacters (theirHash, 12) + ")");
}

// osc_handler_deck_hello: a StemDeck instance names the deck it plays; Core learns where that deck
// listens (its own port, P13), marks it present and tells it its 3D again.
void Core::deckHello (const Endpoint& from, const OscMessage& message)
{
    const auto found = knownAddress (from, message);
    if (! found)
        return;
    seen (message, firstOrNone (message), peerName (from.host, peerHosts));
    const int deck = found->second.at ("deck");
    const auto now = env.clock.monotonic();
    const auto known = deckHosts.find (deck);
    if (known == deckHosts.end() || known->second != from.host)
    {
        deckHosts[deck] = from.host;
        deckClients.insert_or_assign (deck, Client { std::string ("stemdeck-") + static_cast<char> ('A' + deck - 1),
                                                     deckEndpoint (truth, from.host, deck) });
        say ("stemdeck: deck " + std::to_string (deck) + " at " + from.host);
    }
    decks.heard (deck, now);
    const auto channel = decks.channelOf (deck);
    const auto threeD = channel ? channels[static_cast<size_t> (*channel - 1)].threeD : std::nullopt;
    sendToDeck (decks.helloMessages (truth, deck, threeD.value_or (DeckSelector::neutralThreeD)));
    reportDecks (false);
}

// osc_handler_engine_deck_audio: the engine's word on whether it hears a deck's audio, with each hello and on
// change. A deck the engine does not hear is not present, however fresh its own hello (R11).
void Core::engineDeckAudio (const Endpoint& from, const OscMessage& message)
{
    const auto found = knownAddress (from, message);
    if (! found)
        return;
    seen (message, firstOrNone (message), peerName (from.host, peerHosts));
    const auto linked = message.args.empty() ? std::nullopt : pythonFloat (message.args[0]);
    if (! linked)
        return;
    decks.heardAudio (found->second.at ("deck"), *linked != 0.0, env.clock.monotonic());
    reportDecks (false);
}

//==============================================================================
// StemDeck.

// stemdeck_said_hello: when it is news, learn where StemDeck listens and ask it for everything.
void Core::stemdeckSaidHello (const std::string& host, double now)
{
    const bool news = stemdeckWatch.hello (host, now);
    for (const auto& rival : stemdeckWatch.takeRivals())
    {
        const auto followed = stemdeckWatch.host().value_or ("None");
        say ("stemdeck: a second StemDeck at " + rival + " while following " + followed + " -- only one may run; still following " + followed);
    }
    if (! news)
        return;
    const Endpoint target { host, truth.endpoint ("stemdeck", "osc").port };
    stemdeckClient = Client { "stemdeck", target };
    stemdeckTarget = target;
    send (*stemdeckClient, Message { truth.address ("stemdeck.recall"), pyInt (1) });
    send (*stemdeckClient, Message { truth.address ("prodj.live"), pyInt (liveValue()) });
    tellProdjWhere ("stemdeck", host);
}

// notice_stemdeck_silence: a minute without StemDeck's hello, nothing is on its buses any more; each
// channel keeps its source.
void Core::noticeStemdeckSilence (double now)
{
    if (! stemdeckWatch.silence (now))
        return;
    say ("stemdeck: silent for a minute, its buses are empty");
    stemdeckClient.reset();
    stems.forget();
    speakStems (false);
}

// forward_ahead: StemDeck's preview, on to the Motion Core follows -- unchanged, and never noted: a
// preview repeats every bar on purpose, so it does not go through the relay.
void Core::forwardAhead (const OscMessage& message, const std::string& origin)
{
    const auto arguments = aheadArguments (message.args);
    if (! arguments)
    {
        trafficTable.unknownList (message.address, message.args, origin, env.clock.monotonic());
        return;
    }
    send (motionClient, OscMessage { message.address, sentOnAsPython (*arguments) });
}

//==============================================================================
// Motion.

// motion_said_hello: one Motion at a time; every hello of the one followed tells the pro-DJ apps.
void Core::motionSaidHello (const std::string& host, double now)
{
    const auto moved = motion.hello (host, now);
    for (const auto& rival : motion.takeRivals())
    {
        const auto followed = motion.followed().value_or ("None");
        say ("motion: a second Motion at " + rival + " while following " + followed + " -- only one may run; still following " + followed);
    }
    if (moved)
        pointMotionAt (*moved, "said hello");
    if (motion.follows (host))
        tellProdjWhere ("motion", host);
    send (motionClient, Message { truth.address ("prodj.live"), pyInt (liveValue()) });
}

// notice_motion_silence: a minute without the remote Motion's hello, back to the rig's own.
void Core::noticeMotionSilence (double now)
{
    if (const auto moved = motion.silence (now))
        pointMotionAt (*moved, "the remote one is silent, back to the rig's");
}

// point_motion_at: Motion's slot among the subscribers, not a second entry, and the whole state at
// once -- a Motion that has just come up knows nothing.
void Core::pointMotionAt (const Endpoint& endpoint, const std::string& why)
{
    motionClient = Client { "motion", endpoint };
    replaceNamed (subscribers, motionClient);
    peerHosts["motion"] = endpoint.host;
    const auto told = sayTheWholeState();
    say ("motion: " + why + ", now " + endpoint.host + ":" + std::to_string (endpoint.port) + "; replayed " + std::to_string (told) + " messages");
}

//==============================================================================
// The desks and pro-DJ apps.

std::optional<Client> Core::deskClientOf (const std::string& host) const
{
    for (const auto& [deskHost, client] : deskClients)
        if (deskHost == host)
            return client;
    return std::nullopt;
}

// desk_said_hello: a desk with Core's own truth is there. One that has just arrived gets a slot among
// the subscribers and the whole state at once, and LIVE; a pro-DJ app also where the devices run.
void Core::deskSaidHello (const std::string& host, double now, const std::string& kind)
{
    subscribersOwner.check ("a desk joining");
    auto verdict = desks.hello (host, now, kind);
    if (verdict == DeskVerdict::kindChanged)
    {
        // Its client is named by its kind: kept, relay() would not leave the sender out.
        say ("desk: " + host + " now says " + kind + ", joins again as " + deskName (host, kind));
        if (deskLeaves (host))
            liveOffWhenNoProdjIsLeft();
        verdict = DeskVerdict::joined;
    }
    if (verdict == DeskVerdict::known)
        if (const auto client = deskClientOf (host))
            send (*client, Message { truth.address ("prodj.live"), pyInt (liveValue()) });
    if (verdict == DeskVerdict::refusedFirst)
        say ("desk: " + host + " said hello, but " + std::to_string (maxDesks) + " desks are followed already ("
             + joined (desks.hosts(), ", ") + ") -- not followed");
    if (verdict != DeskVerdict::joined)
        return;
    const Client client { deskName (host, kind), { host, desks.port } };
    deskClients.emplace_back (host, client);
    addSubscriber (subscribers, client);
    const auto told = sayTheWholeState();
    if (kind == prodjKind)
        tellProdj (client);
    else
        send (client, Message { truth.address ("prodj.live"), pyInt (liveValue()) });
    say ("desk: " + host + " joined, " + std::to_string (deskClients.size()) + " of " + std::to_string (maxDesks) + "; replayed "
         + std::to_string (told) + " messages");
}

// notice_desk_silence: a minute without a desk's hello, it is told nothing more.
void Core::noticeDeskSilence (double now)
{
    subscribersOwner.check ("a desk leaving");
    bool prodjLeft = false;
    for (const auto& host : desks.silence (now))
    {
        prodjLeft = deskLeaves (host) || prodjLeft;
        say ("desk: " + host + " silent for a minute, no longer followed");
    }
    if (prodjLeft)
        liveOffWhenNoProdjIsLeft();
}

// desk_leaves: its client out of the subscribers; true when it was a pro-DJ app.
bool Core::deskLeaves (const std::string& host)
{
    const auto leaving = std::find_if (deskClients.begin(), deskClients.end(), [&] (const auto& entry) { return entry.first == host; });
    if (leaving == deskClients.end())
        return false;
    const auto name = leaving->second.name;
    deskClients.erase (leaving);
    dropNamed (subscribers, name);
    return kindOf (name) == prodjKind;
}

// asker_client: the client of whoever sent this -- a followed desk, or Motion.
std::optional<Client> Core::askerClient (const Endpoint& from) const
{
    const auto origin = originOf (from);
    if (kindOf (origin))
        return deskClientOf (from.host);
    if (origin == "motion")
        return motionClient;
    return std::nullopt;
}

// prodj_clients: the desks of kind prodj, the only ones the positions and the places go to.
std::vector<Client> Core::prodjClients() const
{
    std::vector<Client> clients;
    for (const auto& host : desks.hostsOfKind (prodjKind))
        if (const auto client = deskClientOf (host))
            clients.push_back (*client);
    return clients;
}

// send_position_to_prodj: one clamped position, to the pro-DJ apps only.
void Core::sendPositionToProdj (const std::string& key, int index, double value)
{
    const auto clients = prodjClients();
    if (clients.empty())
        return;
    const OscMessage message { truth.address (key, { { "ch", index + 1 } }), { pyFloat (value) } };
    for (const auto& client : clients)
        send (client, message);
}

// tell_prodj_where: a device runs at `host`, to every pro-DJ app.
void Core::tellProdjWhere (const std::string& device, const std::string& host)
{
    for (const auto& client : prodjClients())
        send (client, OscMessage { truth.address ("core.device-at"),
                                   { pyStr (device), pyStr (hostToTellOf (host, truth.host ("core"), motionOwnHosts)) } });
}

// tell_prodj: where Motion and StemDeck run, and LIVE.
void Core::tellProdj (const Client& client)
{
    const std::vector<std::pair<std::string, std::optional<std::string>>> places {
        { "motion", motion.followed() }, { "stemdeck", stemdeckClient ? stemdeckWatch.host() : std::nullopt } };
    for (const auto& [device, host] : places)
        if (host)
            send (client, OscMessage { truth.address ("core.device-at"),
                                       { pyStr (device), pyStr (hostToTellOf (*host, truth.host ("core"), motionOwnHosts)) } });
    send (client, Message { truth.address ("prodj.live"), pyInt (liveValue()) });
}

//==============================================================================
// LIVE and the pointers.

// osc_handler_prodj_live: a desk, a pro-DJ app or Motion switches LIVE; Motion only off, and on only
// while a pro-DJ app is followed.
void Core::prodjLiveHandler (const Endpoint& from, const OscMessage& message)
{
    const auto origin = originOf (from);
    const auto* raw = firstOrNone (message);
    seen (message, raw, origin);
    const auto wanted = liveWanted (raw);
    if (! wanted)
    {
        trafficTable.unknown (message.address, raw, origin, env.clock.monotonic());
        return;
    }
    if (! maySwitch (origin, *wanted, ! desks.hostsOfKind (prodjKind).empty()))
        return;
    if (prodjLive.set (*wanted, env.clock.monotonic()))
    {
        say (std::string ("prodj: LIVE ") + (*wanted ? "on" : "off") + " (by " + origin + ")");
        announceLive();
    }
}

// live_off_when_no_prodj_is_left: LIVE off once no pro-DJ app is followed any more, said to all.
void Core::liveOffWhenNoProdjIsLeft()
{
    if (! desks.hostsOfKind (prodjKind).empty() || ! prodjLive.nobodyLeft())
        return;
    say ("prodj: LIVE off, no pro-DJ app left");
    announceLive();
}

// announce_live: LIVE to every desk, Motion, StemDeck and each deck's own port -- not through the
// subscribers' door: the hardware mixer is not told.
void Core::announceLive()
{
    std::vector<Client> listeners;
    for (const auto& [host, client] : deskClients)
        listeners.push_back (client);
    listeners.push_back (motionClient);
    if (stemdeckClient)
        listeners.push_back (*stemdeckClient);
    for (const auto& client : deckListeners())
        listeners.push_back (client);
    for (const auto& client : listeners)
        send (client, Message { truth.address ("prodj.live"), pyInt (liveValue()) });
}

// deck_listeners: each deck's own port, for a word meant for StemDeck as a whole. Deck A may be the
// base port itself: never twice to one host:port.
std::vector<Client> Core::deckListeners() const
{
    std::vector<Endpoint> taken;
    if (stemdeckClient && stemdeckTarget)
        taken.push_back (*stemdeckTarget);
    std::vector<Client> clients;
    for (const auto& [deck, client] : deckClients)   // a std::map: sorted by deck
    {
        const auto target = deckEndpoint (truth, deckHosts.at (deck), deck);
        if (std::find (taken.begin(), taken.end(), target) != taken.end())
            continue;
        taken.push_back (target);
        clients.push_back (client);
    }
    return clients;
}

// osc_handler_pointer: a pro-DJ app's click on StemDeck's or Motion's screen picture, on to that
// device -- only while LIVE is on, only from a pro-DJ app, through neither door.
void Core::pointerHandler (const Endpoint& from, const OscMessage& message)
{
    const auto found = knownAddress (from, message);
    if (! found)
        return;
    const auto& key = found->first;
    const auto origin = originOf (from);
    // Seen before the gate: a refused pointer is an arrival with no send after it. A list in B1, always.
    trafficTable.seenList (Direction::in, message.address, message.args, origin, env.clock.monotonic());
    if (! prodjLive.admits (saidKinds.gateName (origin, from.host, from.port), key))
        return;
    const auto target = key == "stemdeck.pointer" ? stemdeckClient : std::optional<Client> (motionClient);
    if (target)
        send (*target, OscMessage { message.address, sentOnAsPython (message.args) });
}

//==============================================================================
// The beat and the meters.

// osc_handler_beat: the analyzer's clock. A remote Motion and the desks get it unchanged, every beat
// (the analyzer reaches only this machine); the tempo, once it holds still, goes to the engine's delay.
void Core::beatHandler (const Endpoint& from, const OscMessage& message)
{
    const auto* tempoArg = message.args.size() > 2 ? &message.args[2] : nullptr;
    trafficTable.seen (Direction::in, message.address, tempoArg, peerName (from.host, peerHosts, commanders), env.clock.monotonic());

    const OscMessage passedOn { message.address, sentOnAsPython (message.args) };
    if (motion.forwardsBeat())
        send (motionClient, passedOn);
    const auto told = deskClients;   // list(_desk_clients.values())
    for (const auto& [host, client] : told)
        send (client, passedOn);

    const auto wanted = tempoArg != nullptr ? tempo.beat (*tempoArg, env.clock.monotonic()) : std::nullopt;
    if (wanted)
        sendTempo (*wanted);
    else
        followDeckTempo();   // a beat without a tempo keeps no clock alive
    // B1 also set the DualDelay's two lines inside REAPER: not ported (O8).
}

// stemdeck.tempo: a StemDeck instance's word on its deck's tempo, from whichever host plays the deck
// (a one-deck instance on a deck host, the two-deck app on the Core), as the deck hello is taken.
void Core::deckTempo (const Endpoint& from, const OscMessage& message)
{
    const auto found = knownAddress (from, message);
    if (! found)
        return;
    const auto origin = peerName (from.host, peerHosts);
    seen (message, firstOrNone (message), origin);
    if (! tempo.deckReport (found->second.at ("deck"), message.args, env.clock.monotonic()))
    {
        trafficTable.unknown (message.address, firstOrNone (message), origin, env.clock.monotonic());
        return;
    }
    followDeckTempo();
}

// Without a live beat clock the decks set the delay's tempo (Ruling A4); with one, this does nothing.
void Core::followDeckTempo()
{
    if (const auto wanted = tempo.fromDecks (env.clock.monotonic(), [this] (int deck) { return faderOfDeck (deck); }))
        sendTempo (*wanted);
}

// The fader of the channel the deck is on; a channel whose fader Core has not heard yet counts as down.
std::optional<double> Core::faderOfDeck (int deck) const
{
    const auto channel = decks.channelOf (deck);
    if (! channel)
        return std::nullopt;
    const auto fader = relayed.value (truth.address ("channel.volume", { { "ch", *channel } }));
    return fader ? pythonFloat (*fader).value_or (0.0) : 0.0;
}

// Kept for the engine's next hello, which gets it again.
void Core::sendTempo (double bpm)
{
    engineBpm = bpm;
    sendToEngine ({ tempoMessage (truth, bpm) });
    reannounceBeatIfItPlaysDifferently();
}

// vu_destinations: the remote Motion followed (the rig's own gets the analyzer's meters directly)
// and every desk followed.
std::vector<Endpoint> Core::vuDestinations() const
{
    // The followers' state: B1 read it from the relay's own thread; here only the serve thread may (P8).
    subscribersOwner.check ("the meters' destinations");
    std::vector<Endpoint> destinations;
    if (const auto remote = motion.vuDestination())
        destinations.push_back (*remote);
    for (const auto& desk : desks.vuDestinations())
        destinations.push_back (desk);
    return destinations;
}

// a3_core_vu_relay.forward, one datagram: the bytes unread, to every vu destination. B1 runs it on a
// thread of its own; here the service hands each datagram to the serve thread (P8). O13: the engine's
// own meters also go to the displays the analyzer used to reach (the truth's `core` routes that carry
// vu), each endpoint once; StemDeck's as B1 passes them. A datagram that is no meter is noted as an
// unknown address and goes on to the followed ones as before: nothing is held back that B1 sent.
void Core::handleVu (const Endpoint& from, const uint8_t* data, size_t size)
{
    const auto source = vuSourceOf (env.truth, data, size);
    if (source == VuSource::unknown)
        trafficTable.unknown (env.truth.pattern ("vu"), nullptr, from.host, env.clock.monotonic());
    bool handed = false;
    forward ([&]() -> std::optional<Datagram> {
                 handed = true;
                 return Datagram (data, data + size);
             },
             [this] (const Datagram& datagram, const Endpoint& to) { env.out.sendRaw (Client { "vu", to }, datagram); },
             [&] {
                 auto targets = source == VuSource::engine ? engineVuTargets (env.truth) : std::vector<Endpoint> {};
                 for (const auto& followed : vuDestinations())
                     appendOnce (targets, followed);
                 return targets;
             },
             [&] { return ! handed; });
}
}
