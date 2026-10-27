// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The recall and the engine's replay, from B1's a3-core.py (a3-core 7a7b2441f74e), engine mode: the
// whole state said again to every subscriber, and a new engine (a boot id not heard before) given the
// evening through Core's own port, then Core's token recall, which sends the engine its state and
// opens it. B1 runs the replay in a thread of its own; here it is sent from the serve thread to Core's
// own port (P8), and handled there in order behind it.
#include "Core.h"

#include "CorePython.h"
#include "EngineWords.h"
#include "Evening.h"
#include "Json.h"

namespace a3core
{
// Every value to every subscriber, as the messages it would have arrived as: not through broadcast(),
// which drops a value already passed on -- a recall is somebody who knows nothing.
int Core::sayTheWholeState()
{
    const auto messages = recallMessages (truth, channels, master, relayed);
    for (const auto& message : messages)
        for (const auto& client : subscribers)
            send (client, message);
    speakStems (true);
    return static_cast<int> (messages.size());
}

// osc_handler_recall: the whole state again, to every subscriber; Core's own recall after the
// replay also sends the engine what the evening does not carry, then opens it.
void Core::recall (const Endpoint& from, const OscMessage& message)
{
    const auto origin = originOf (from);
    seen (message, firstOrNone (message), origin);
    const auto told = sayTheWholeState();
    say (message.address + ": replayed " + std::to_string (told) + " messages to " + std::to_string (subscribers.size()) + " subscribers");

    if (const auto asker = askerClient (from))
    {
        if (kindOf (origin) == prodjKind)
            tellProdj (*asker);
        else
            send (*asker, Message { truth.address ("prodj.live"), pyInt (liveValue()) });
    }
    if (startupRecall.isIt (message.args))
    {
        sendToEngine (stateMessages (truth, channels, master, decks, engineReturnSource (stems.returnMode), engineBpm));
        sendToEngine ({ openMessage (truth) });
    }
}

// osc_handler_engine_hello: a new boot id is an engine that knows nothing. The evening is replayed
// through Core's own door, then Core's token recall sends the state and opens. B1 runs the replay in
// a thread; here it is sent from the serve thread to Core's own port (P8).
void Core::engineHello (const Endpoint& from, const OscMessage& message)
{
    seen (message, firstOrNone (message), originOf (from));
    if (message.args.empty())
        return;
    const auto bootId = pythonStr (message.args.front());
    if (! project->heardHello (pyStr (bootId)))
        return;
    const auto roundStarted = project->round;
    say ("engine: hello from " + bootId + ", replaying the evening");

    const auto evening = theEveningToReplay();   // replay_evening
    for (const auto& [address, value] : evening)
        sendToSelf (address, value);
    say ("startup: replayed " + std::to_string (evening.size()) + " values from " + eveningPath.getFullPathName().toStdString());
    startupRecall.rearm (env.newToken());
    sendToSelf (truth.address ("state.recall"), pyInt (startupRecall.token()));
    if (project->replayed (roundStarted))
        writeTheEvening();
}

// the_evening_to_replay: the session's evening with what the desk sent this run laid over it --
// dict(evening), then the desk's values replacing or added -- as replayable() reads it.
std::vector<Message> Core::theEveningToReplay() const
{
    auto* values = new juce::DynamicObject();
    for (const auto& [address, value] : project->evening())
        values->setProperty (juce::String::fromUTF8 (address.c_str()), jsonOf (value));
    for (const auto& [address, number] : deskFirst.over ({}))
        values->setProperty (juce::String::fromUTF8 (address.c_str()), number);
    auto* state = new juce::DynamicObject();
    state->setProperty ("values", juce::var (values));
    return replayable (truth, juce::var (state));
}
}
