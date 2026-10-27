// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The router of B1's a3-core.py (a3-core 7a7b2441f74e), engine mode: what arrives on Core's main port
// and where it goes -- the values, the flags and their lamps, the relay to the other screens, the
// engine's words, the deck ring, the aux return's ring, and the hellos and the recall that open the
// way to the engine. The function each method comes from is named beside it. REAPER's send points
// (osc_reaper, the IEM clients, the DualDelay, send_cue_levels, set_filters) reach nobody in engine
// mode and are not ported (O8): where a branch only sent to them, it is empty here.
#include "Core.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "Ahead.h"
#include "Buttons.h"
#include "CorePython.h"
#include "CoreState.h"
#include "Evening.h"
#include "Json.h"
#include "OscWire.h"
#include "StemsWords.h"

namespace a3core
{
namespace
{
// int(x) of a float, for a turn of `ringSize` places: only where the cursor lands matters, so the
// steps are kept modulo the ring (exactly: fmod of a whole double is exact). int(nan) is a
// ValueError, int(inf) an OverflowError.
int turnSteps (double value, int ringSize)
{
    if (std::isnan (value))
        throw PythonError ("ValueError");
    if (std::isinf (value))
        throw PythonError ("OverflowError");
    return static_cast<int> (std::fmod (std::trunc (value), static_cast<double> (ringSize)));
}

// float(max(min(value, top), -top)) with Python's min and max: a NaN passes through both.
double clamped (double value, double top)
{
    const double lower = top < value ? top : value;
    return -top > lower ? -top : lower;
}

// _is_int of a3_core_stems: an int, not a bool (python-osc's i, h and r are ints).
std::optional<long long> wholeOf (const OscArg* value)
{
    if (value == nullptr)
        return std::nullopt;
    switch (value->tag)
    {
        case 'i': return std::get<int32_t> (value->value);
        case 'h': case 'r': return std::get<int64_t> (value->value);
        default: return std::nullopt;
    }
}

constexpr int returnRingSize = 3;   // STEM, ANALOG, CUE
const std::string reaperOnly[] { "aux-send", "gain", "eq.high", "eq.mid", "eq.low", "volume", "filter.frequency", "filter.q" };
}

//==============================================================================
// What every value handler asks first.

// known_address: the address taken apart against the one truth, or written down as unknown.
std::optional<Core::Found> Core::knownAddress (const Endpoint& from, const OscMessage& message)
{
    auto found = truth.match (message.address);
    if (! found)
        unrouted (from, message);
    return found;
}

// values_refused: a pro-DJ app moves nothing while LIVE is off; a mixer value from a host that has
// not said hello with Core's truth is ignored, not relayed, not remembered. Edges pass.
bool Core::valuesRefused (const Endpoint& from, const OscMessage& message, const std::string& key)
{
    const auto who = originOf (from);
    if (! prodjLive.admits (saidKinds.gateName (who, from.host, from.port), key))
    {
        seen (message, firstOrNone (message), who);   // an arrival with nothing after it
        return true;
    }
    if (! relayOnArrival (truth, message.address))
        return false;
    switch (helloGate.check (from.host, from.port))
    {
        case GateVerdict::admit: return false;
        case GateVerdict::refuseFirst: say (who + " (" + from.host + "): values before its hello ignored"); break;
        case GateVerdict::refuseDiffersFirst:
            say (who + " (" + from.host + "): values ignored, its a3-osc.json differs from Core's");
            break;
        case GateVerdict::refuse: break;
    }
    return true;
}

// origin_of: a followed desk by its listening socket, everyone else by host.
std::string Core::originOf (const Endpoint& from) const
{
    if (const auto desk = desks.nameOf (from.host, from.port))
        return *desk;
    return peerName (from.host, peerHosts, commanders);
}

void Core::seen (const OscMessage& message, const OscArg* value, const std::string& origin)
{
    trafficTable.seen (Direction::in, message.address, value, origin, env.clock.monotonic());
}

// relay: the number that just arrived, on to every other subscriber; the desk's text becomes a number.
void Core::relay (const std::string& address, const OscArg& raw, const std::string& origin)
{
    if (! relayOnArrival (truth, address))
        return;
    const auto number = pythonFloat (raw);
    if (! number)
        return;   // /fx/mode carries a word: dropped here, not guarded at each call site
    deskFirst.heard (address, *number, origin == "mixer");   // before the repeat is dropped: still the desk's
    const auto value = pyFloat (*number);
    if (relayed.holds (address, value))
        return;
    notePassedOn (address, { value });
    for (const auto& client : everyoneBut (subscribers, origin))
        send (client, OscMessage { address, { value } });
}

// remember_state: the state after a message, offered to the debounced file.
void Core::rememberState()
{
    const auto stemsData = stems.asData();
    stateFile.remember (stateOf (channels, master, &stemsData), env.clock.monotonic(), nanArrived);
    nanArrived = false;
}

// apply_3d_crossfade's memory (its gains are REAPER's), and take_deck_word's.
void Core::keepThreeD (int index, double value)
{
    channels[static_cast<size_t> (index)].threeD = value;
    nanArrived = nanArrived || std::isnan (value);
}

//==============================================================================
// The handlers.

// osc_handler_channel
void Core::channelHandler (const Endpoint& from, const OscMessage& message)
{
    const auto found = knownAddress (from, message);
    if (! found || valuesRefused (from, message, found->first))
        return;
    const auto& [key, fields] = *found;
    // The buttons read the argument as it arrived: its type tells the desk's edge from Motion's state.
    const auto& raw = firstOf (message);
    const double value = floatOf (raw);

    const auto origin = originOf (from);
    seen (message, &raw, origin);
    if (args.printOsc)
        say (message.address + " : " + pythonRepr (value));
    relay (message.address, raw, origin);
    sendToEngine (engineChannelMessages (truth, decks, key, fields, value));

    const int channel = fields.at ("ch");
    const int index = channel - 1;
    const auto parameter = key.substr (std::string ("channel.").size());
    if (takeDeckWord (parameter, channel, value))
    {
        rememberState();
        return;
    }

    if (parameter == "3d")
        keepThreeD (index, value);
    else if (std::find (std::begin (reaperOnly), std::end (reaperOnly), parameter) != std::end (reaperOnly))
    {
        // REAPER's: the engine heard it above, as it arrived.
    }
    else if (parameter == "cue" || parameter == "filter")
    {
        auto& info = channels[static_cast<size_t> (index)];
        const bool cue = parameter == "cue";
        auto& toggle = cue ? info.toggleCue : info.toggleFx;
        const auto wanted = wantedToggle (raw, toggle);
        if (const auto* on = std::get_if<bool> (&wanted))
        {
            toggle = *on;
            announceFlag (cue ? LedFlag::cue : LedFlag::fx, index);
            sendToEngine ({ flagMessage (truth, cue ? EngineFlag::cue : EngineFlag::filter, index, *on) });
        }
    }
    else if (parameter == "azimuth" || parameter == "elevation")
    {
        const bool azimuth = parameter == "azimuth";
        // The clamped value, not the one that arrived: a recall replays what was actually told.
        const double degrees = clamped (value, azimuth ? 180.0 : 90.0);
        auto& info = channels[static_cast<size_t> (index)];
        (azimuth ? info.azimuth : info.elevation) = degrees;
        if (! decks.deckChannel (channel))
            sendToEngine ({ positionMessage (truth, azimuth ? Position::azimuth : Position::elevation, index, degrees) });
        sendPositionToProdj (key, index, degrees);
    }
    else
    {
        // Handled and not served: the window must not list it as understood.
        const auto number = pyFloat (value);
        trafficTable.unknown (message.address, &number, origin, env.clock.monotonic());
    }

    rememberState();
}

// osc_handler_master
void Core::masterHandler (const Endpoint& from, const OscMessage& message)
{
    const auto found = knownAddress (from, message);
    if (! found || valuesRefused (from, message, found->first))
        return;
    const auto& key = found->first;
    const auto& raw = firstOf (message);
    const double value = floatOf (raw);

    const auto origin = originOf (from);
    seen (message, &raw, origin);
    if (args.printOsc)
        say (message.address + " : " + pythonRepr (value));
    relay (message.address, raw, origin);
    sendToEngine (forwardMessages (truth, key, {}, value));

    if (key == "master.phones-mix")
    {
        master.phonesMix = value;
        nanArrived = nanArrived || std::isnan (value);
        rememberState();
    }
}

// osc_handler_filter
void Core::filterHandler (const Endpoint& from, const OscMessage& message)
{
    const auto found = knownAddress (from, message);
    if (! found || valuesRefused (from, message, found->first))
        return;
    const auto& key = found->first;
    const auto& raw = firstOf (message);

    const auto origin = originOf (from);
    seen (message, &raw, origin);
    if (args.printOsc)
        say (message.address + " : " + pythonStr (raw));
    relay (message.address, raw, origin);
    if (forwarded().count (key) != 0)   // forward_messages reads float(value) only for the words it forwards
        sendToEngine (forwardMessages (truth, key, {}, floatOf (raw)));

    if (key == "filter.mode")
    {
        // The desk names the mode, Motion sends the number its key reads; said back twice, to everybody.
        const auto wanted = wantedFxMode (raw, fxModeWords.at (master.fxMode));
        master.fxMode = wanted == fxModeHighPass ? FxMode::highPass : FxMode::lowPass;
        broadcast ({ truth.address ("filter.led"), { pyStr (fxModeWords.at (master.fxMode)) } });
        broadcast ({ truth.address ("filter.mode"), { pyFloat (fxModeNumbers.at (master.fxMode)) } });
        sendToEngine ({ filterModeMessage (truth, static_cast<int> (master.fxMode)) });
    }
    rememberState();
}

// osc_handler_aux_return: turn moves the cursor over STEM, ANALOG and CUE; push switches the mode, or
// on CUE toggles the return's cue.
void Core::auxReturnHandler (const Endpoint& from, const OscMessage& message)
{
    const auto found = knownAddress (from, message);
    if (! found || valuesRefused (from, message, found->first))
        return;
    const auto& key = found->first;
    const auto* value = firstOrNone (message);
    const auto origin = originOf (from);
    seen (message, value, origin);
    if (key == "aux-return.stem.turn")
        stems.turn (Stems::returnIndex, turnSteps (floatOf (firstOf (message)), returnRingSize));
    else if (key == "aux-return.stem.push")
    {
        // Its commands would go to StemDeck, which engine mode never sends (send_to_stemdeck).
        stems.push (Stems::returnIndex, stemdeckClient.has_value());
        if (stems.returnCursor == Stems::cueField)
            toggleReturnCue();
    }
    else
    {
        trafficTable.unknown (message.address, value, origin, env.clock.monotonic());
        return;
    }
    speakStems (false);
    rememberState();
}

// osc_handler_stemdeck: StemDeck's report of one stem's bus switches, and its preview, which goes on to
// Motion (forward_ahead). tidy_when_settled returns at once in engine mode, so the report's settle is
// not kept.
void Core::stemdeckHandler (const Endpoint& from, const OscMessage& message)
{
    const auto found = knownAddress (from, message);
    if (! found)
        return;
    const auto& [key, fields] = *found;
    if (key == "stemdeck.pointer" || key == "stemdeck.deck-hello")
        return;   // osc_handler_pointer's / _deck_hello's, mapped beside this family
    const auto* value = firstOrNone (message);
    const auto origin = peerName (from.host, peerHosts);
    seen (message, value, origin);
    if (key == "stemdeck.ahead")
    {
        if (fromTheFollowed (from.host, stemdeckWatch.host()))
            forwardAhead (message, origin);
        return;
    }
    if (key != "stemdeck.buses" || ! takeStemReport (fields, value))
    {
        trafficTable.unknown (message.address, value, origin, env.clock.monotonic());
        return;
    }
    speakStems (false);
}

// _stems.report(pair_of(deck, stem), value): an int only -- Stems::report takes ints, so the types B1's
// report refuses (a float, a bool, a text, None) are refused here.
bool Core::takeStemReport (const std::map<std::string, int>& fields, const OscArg* value)
{
    const auto mask = wholeOf (value);
    if (! mask || *mask < 0 || *mask > std::numeric_limits<int>::max())
        return false;
    return stems.report (pairOf (fields.at ("deck"), fields.at ("stem")), static_cast<int> (*mask));
}






//==============================================================================
// The flags, the decks, the return.

// announce_flag: the lamp and the flag, both to everybody; broadcast() drops what did not move.
void Core::announceFlag (LedFlag flag, int index)
{
    const auto& info = channels[static_cast<size_t> (index)];
    const auto lamp = ledMessage (truth, flag, index, info);
    broadcast ({ lamp.first, { lamp.second } });
    const auto setting = flagMessage (truth, flag == LedFlag::cue ? EngineFlag::cue : EngineFlag::filter, index,
                                      flag == LedFlag::cue ? info.toggleCue : info.toggleFx);
    broadcast ({ setting.first, { setting.second } });
}

// toggle_return_cue: its lamp to everybody, its cue to the engine.
void Core::toggleReturnCue()
{
    master.returnCue = ! master.returnCue;
    const auto lamp = returnCueLamp (truth, master);
    broadcast ({ lamp.first, { lamp.second } });
    sendToEngine ({ returnCueMessage (truth, master.returnCue) });
}

// take_deck_word: the desk's ring words, and the 3D of a channel playing a deck, are the deck selector's.
bool Core::takeDeckWord (const std::string& parameter, int channel, double value)
{
    if (parameter == "stem.turn" || parameter == "stem.push")
    {
        selectDeck (parameter, channel, value);
        return true;
    }
    if (parameter == "3d" && decks.deckChannel (channel))
    {
        keepThreeD (channel - 1, value);
        sendToDeck (decks.threeDMessages (truth, channel, value));
        return true;
    }
    return false;
}

// select_deck: a turn moves the cursor, a push puts a deck (or analog) on the channel.
void Core::selectDeck (const std::string& word, int channel, double value)
{
    if (word == "stem.turn")
    {
        decks.turn (channel, turnSteps (value, static_cast<int> (decks.ring (channel).size())));
        reportDecks (false);
        return;
    }
    const auto changes = decks.push (channel);
    auto toDecks = decks.leaveMessages (truth, changes);   // straight after the push: it reads what the push saved
    if (changes.empty())
        return;
    for (const auto& [onChannel, source] : changes)
    {
        const auto& threeD = channels[static_cast<size_t> (onChannel - 1)].threeD;
        if (source != DeckSelector::analog && threeD)
            for (auto& arriving : decks.threeDMessages (truth, onChannel, *threeD))
                toDecks.push_back (std::move (arriving));
    }
    speakStems (false);
    sendToDeck (toDecks);
    // The engine's analog path kept the values from before the deck came; the source word is out.
    for (const auto& [onChannel, source] : changes)
        if (source == DeckSelector::analog)
            sendToEngine (analogAgainMessages (truth, onChannel - 1, channels[static_cast<size_t> (onChannel - 1)], relayed));
}

// send_to_deck: each /stemdeck/{deck}/... word to the host its deck said hello from.
void Core::sendToDeck (const std::vector<Message>& messages)
{
    for (const auto& message : messages)
    {
        const auto found = truth.match (message.first);
        if (! found)
            continue;
        const auto deck = found->second.find ("deck");
        if (deck == found->second.end())
            continue;
        if (const auto client = deckClients.find (deck->second); client != deckClients.end())
            send (client->second, message);
    }
}
}
