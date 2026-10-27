// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The Core: B1's a3-core.py (a3-core 7a7b2441f74e) in engine mode, as one object. Every method is
// called from one thread, the serve thread (P8): handlers, ticks, debounce flushes and the engine's
// replay -- the replay is sent to Core's own port from there, never from a thread of its own.
// Sockets, the window and the announce thread are the service's (CoreService, Task 20); this object
// only ever talks through the Outbox, the Clock and the Journal it is given.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <juce_core/juce_core.h>

#include "Channels.h"
#include "Decks.h"
#include "DeskFirst.h"
#include "Desks.h"
#include "Devices.h"
#include "Dispatcher.h"
#include "EngineWords.h"
#include "HelloGate.h"
#include "MotionTarget.h"
#include "Net.h"
#include "Presence.h"
#include "ProdjLive.h"
#include "Recall.h"
#include "SeenFile.h"
#include "SendFailures.h"
#include "StartupRecall.h"
#include "StateFile.h"
#include "Stems.h"
#include "Subscribers.h"
#include "Traffic.h"
#include "Truth.h"

namespace a3core
{
struct CoreArgs
{
    std::string ip;
    int port;
    Endpoint mixer, motion;
    std::vector<std::string> subscribers;   // NAME=HOST:PORT, as --subscriber
    bool printOsc = false;
};

struct CoreEnvironment
{
    const Truth& truth;
    Outbox& out;
    Clock& clock;
    Journal journal;
    juce::File stateDir;   // XDG's state directory; Core's files are in its a3-core/
    std::function<std::string()> newBootId;
    std::function<long long()> newToken;   // the start-up recall's token, an OSC int as the truth says
    Endpoint selfSocket;                   // where Core's own replays come from: trusted by the hello gate
    // Told after every write of state.json, evening.json or seen.json (the scenario transcript).
    std::function<void (const juce::File&)> fileWritten = {};
};

// The words whose older values are worth nothing once a newer one waits (B1's is_position).
bool isPosition (const Truth& truth, const std::string& address);

class Core
{
public:
    Core (CoreEnvironment, CoreArgs);

    void start();                                                     // B1's start-up, engine mode
    void handlePacket (const Endpoint& from, const uint8_t*, size_t);
    void handleVu (const Endpoint& from, const uint8_t*, size_t);     // core.vu-relay datagrams (Task 22)
    void tick();                                                      // on_tick + debounce flushes, every 0.1 s
    void stop();                                                      // flush state, evening, seen

    juce::var statusJson() const;

    Traffic& traffic() { return trafficTable; }
    Devices& devices() { return deviceTable; }

private:
    // The doors out (B1's WatchedClient, broadcast, note_passed_on).
    void send (const Client& client, const OscMessage& message);
    void send (const Client& client, const Message& message);
    void toEverySubscriber (const OscMessage& message);
    void broadcast (const OscMessage& message);
    void notePassedOn (const std::string& address, const std::vector<OscArg>& value);   // several: a Python list
    void writeTheEvening();
    bool engineHeard() const;
    void sendToEngine (const std::vector<Message>& messages);
    void say (const std::string& line, bool toStderr = false);

    // What is said whole: at start-up, to a recall, to a Motion that moved.
    void speakRememberedState();
    int sayTheWholeState();
    void speakStems (bool full);
    void reportDecks (bool full);

    // The tick's watchers. A method whose body only says notPortedYet is a path no scenario pins yet:
    // it throws until the task named in it ports it test-first (tests/core/scenarios/README.md).
    void noticeMotionSilence (double now);
    void pointMotionAt (const Endpoint& endpoint, const std::string& why);
    void noticeDeskSilence (double now);
    bool deskLeaves (const std::string& host);
    void liveOffWhenNoProdjIsLeft();
    void announceLive();
    std::vector<Client> deckListeners() const;
    void noticeDeckPresence (double now);
    void flushIfDue (StateFile& file, const juce::File& path, double now);
    void tellWritten (int before, int after, const juce::File& path);

    // The dispatcher's maps (B1 a3-core.py:2297-2342) and the handlers.
    void mapHandlers();
    void unrouted (const Endpoint& from, const OscMessage& message);
    [[noreturn]] void notPortedYet (const char* handler) const;

    // The router (CoreValues.cpp): the handlers of Core's main port and what they share.
    void channelHandler (const Endpoint& from, const OscMessage& message);
    void masterHandler (const Endpoint& from, const OscMessage& message);
    void filterHandler (const Endpoint& from, const OscMessage& message);
    void auxReturnHandler (const Endpoint& from, const OscMessage& message);
    void stemdeckHandler (const Endpoint& from, const OscMessage& message);
    void deviceHello (const Endpoint& from, const OscMessage& message);
    void engineHello (const Endpoint& from, const OscMessage& message);
    void deckHello (const Endpoint& from, const OscMessage& message);
    void recall (const Endpoint& from, const OscMessage& message);

    using Found = std::pair<std::string, std::map<std::string, int>>;   // a truth key and its fields
    std::optional<Found> knownAddress (const Endpoint& from, const OscMessage& message);
    bool valuesRefused (const Endpoint& from, const OscMessage& message, const std::string& key);
    std::string originOf (const Endpoint& from) const;
    void seen (const OscMessage& message, const OscArg* value, const std::string& origin);
    void relay (const std::string& address, const OscArg& raw, const std::string& origin);
    bool takeDeckWord (const std::string& parameter, int channel, double value);
    void selectDeck (const std::string& word, int channel, double value);
    void sendToDeck (const std::vector<Message>& messages);
    void announceFlag (LedFlag flag, int index);
    void toggleReturnCue();
    void keepThreeD (int index, double value);
    void rememberState();
    void sendPositionToProdj (const std::string& key, int index, double value);
    std::vector<Client> prodjClients() const;
    void tellProdjWhere (const std::string& device, const std::string& host);
    void tellProdj (const Client& client);
    std::optional<Client> askerClient (const Endpoint& from) const;
    void stemdeckSaidHello (const std::string& host, double now);
    void motionSaidHello (const std::string& host, double now);
    void deskSaidHello (const std::string& host, double now, const std::string& kind);
    void noticeStemdeckSilence (double now);
    void sendToSelf (const std::string& address, const OscArg& value);
    std::vector<Message> theEveningToReplay() const;
    bool takeStemReport (const std::map<std::string, int>& fields, const OscArg* value);

    int liveValue() const { return prodjLive.on() ? 1 : 0; }

    CoreEnvironment env;
    CoreArgs args;
    const Truth& truth;
    juce::File statePath, eveningPath, seenFilePath;

    Traffic trafficTable;
    Devices deviceTable;
    SeenFile seenFile;
    SendFailures sendFailures;
    HelloGate helloGate;
    std::map<std::string, std::string> peerHosts;

    Client mixerClient, motionClient, engineClient, selfClient;
    std::vector<Client> subscribers;
    std::set<std::string> motionOwnHosts;
    MotionTarget motion;
    DeskFollowers desks;
    std::vector<std::pair<std::string, Client>> deskClients;   // by host, in the order they joined
    SaidKinds saidKinds;
    ProdjLive prodjLive;
    OwningThread subscribersOwner;

    std::vector<ChannelInfo> channels;
    MasterInfo master;
    StateFile stateFile, eveningFile;
    Stems stems;
    Relayed relayed;
    DeskFirst deskFirst;
    StartupRecall startupRecall;

    DeckSelector decks;
    std::map<int, std::string> deckHosts;
    std::map<int, Client> deckClients;
    double decksLookedAt = 0.0;
    std::optional<EngineSession> project;
    std::map<std::string, OscArg> sourcesSentToEngine;
    std::optional<double> engineBpm;
    // A NaN was just put into the state (B1: a new float object, so the state differs from the last).
    bool nanArrived = false;

    std::optional<Client> stemdeckClient;
    std::optional<Endpoint> stemdeckTarget;
    HelloWatch stemdeckWatch;

    Dispatcher dispatcher;
};
}
