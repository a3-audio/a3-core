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
#include "CoreView.h"
#include "Decks.h"
#include "Departments.h"
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
#include "TempoSource.h"
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
    std::vector<Department> departments;    // the window's stored list (Departments.h)
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

    void start();                                                     // B1's start-up, engine mode: both halves
    // The halves, for the service to put its meters' line between them where B1 says it: the
    // peers, the hello gate and the subscribers (a refused one throws SubscriberError), the evening;
    // then the remembered state said, the address list restored, the serve thread's claim.
    void startPeers();
    void startSpeaking();
    // Where Core's own replays go: the main port as it was bound (`--port 0` is the system's pick, known
    // only once the service binds it, after the start-up spoke).
    void serveOn (int port);
    void handlePacket (const Endpoint& from, const uint8_t*, size_t);
    void handleVu (const Endpoint& from, const uint8_t*, size_t);     // core.vu-relay datagrams: the engine's, StemDeck's
    void tick();                                                      // on_tick + debounce flushes, every 0.1 s
    void stop();                                                      // flush state, evening, seen

    // B1's engine_status, and project 2's rig: who is there. Serve thread only (P8) -- an HTTP thread
    // reads view(), which publishView() fills; tick() does that every publishEvery seconds.
    juce::var statusJson() const;
    juce::var rigJson() const;
    void publishView();
    const CoreView& view() const { return published; }
    static constexpr double publishEvery = 0.25;

    // The stored departments, live: an entry that is on and whose name the service file does not give
    // is subscribed (one that joins is told the whole state), one that went or was switched off is told
    // nothing more. Serve thread only.
    void setDepartments (std::vector<Department> stored);
    // {"departments": [...]}: the service file's subscribers (from "service"), then the stored entries
    // (from "window"), each with whether it is subscribed, whether the service file overrides it, how
    // many messages it was sent and how many seconds ago the last one (null: none yet).
    juce::var departmentsJson() const;

    // The bench's mixer and motion: sent as Core's own clients send, so the traffic table has the
    // row. Serve thread only. std::invalid_argument for any other `to`.
    void sendFromBench (const std::string& to, const OscMessage& message);

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
    void applyDepartments (std::vector<Department> stored, bool speak);
    std::vector<std::string> serviceNames() const;

    // What is said whole: at start-up, to a recall, to a Motion that moved.
    void speakRememberedState();
    int sayTheWholeState();
    void speakStems (bool full);
    void reportDecks (bool full);

    // The tick's watchers (CorePeers.cpp, but the decks' presence). A method whose body only says
    // notPortedYet is a path no scenario pins yet: it throws until the task named in it ports it
    // test-first (tests/core/scenarios/README.md).
    void noticeMotionSilence (double now);
    void pointMotionAt (const Endpoint& endpoint, const std::string& why);
    void noticeDeskSilence (double now);
    bool deskLeaves (const std::string& host);
    void liveOffWhenNoProdjIsLeft();
    void announceLive();
    std::vector<Client> deckListeners() const;
    void noticeDeckPresence (double now);
    void lookAtTheTempo (double now);
    void flushIfDue (StateFile& file, const juce::File& path, double now);
    void tellWritten (int before, int after, const juce::File& path);

    // The dispatcher's maps (B1 a3-core.py:2297-2342) and the handlers.
    void mapHandlers();
    void unrouted (const Endpoint& from, const OscMessage& message);
    [[noreturn]] void notPortedYet (const char* handler) const;

    // The router: the handlers of Core's main port and what they share -- the values (CoreValues.cpp),
    // who Core follows and tells (CorePeers.cpp), the recall and the engine's replay (CoreRecall.cpp).
    void channelHandler (const Endpoint& from, const OscMessage& message);
    void masterHandler (const Endpoint& from, const OscMessage& message);
    void filterHandler (const Endpoint& from, const OscMessage& message);
    void auxReturnHandler (const Endpoint& from, const OscMessage& message);
    void stemdeckHandler (const Endpoint& from, const OscMessage& message);
    void deviceHello (const Endpoint& from, const OscMessage& message);
    void engineHello (const Endpoint& from, const OscMessage& message);
    void deviceVersion (const Endpoint& from, const OscMessage& message);
    void deckHello (const Endpoint& from, const OscMessage& message);
    void engineDeckAudio (const Endpoint& from, const OscMessage& message);
    void recall (const Endpoint& from, const OscMessage& message);
    void beatHandler (const Endpoint& from, const OscMessage& message);
    void deckTempo (const Endpoint& from, const OscMessage& message);
    void prodjLiveHandler (const Endpoint& from, const OscMessage& message);
    void pointerHandler (const Endpoint& from, const OscMessage& message);
    void forwardAhead (const OscMessage& message, const std::string& origin);
    std::vector<Endpoint> vuDestinations() const;
    void followDeckTempo();
    std::optional<double> faderOfDeck (int deck) const;
    void sendTempo (double bpm);

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
    void stepDelay (DelayWord word, long long clicks);
    void announceDelayWord (DelayWord word);
    void tellTheDeskDelayWord (DelayWord word);
    void announceDelayRecall();
    void reannounceBeatIfItPlaysDifferently();
    void keepThreeD (int index, double value);
    void rememberState();
    void sendPositionToProdj (const std::string& key, int index, double value);
    std::vector<Client> prodjClients() const;
    void tellProdjWhere (const std::string& device, const std::string& host);
    void tellProdj (const Client& client);
    std::optional<Client> askerClient (const Endpoint& from) const;
    std::optional<Client> deskClientOf (const std::string& host) const;
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
    std::vector<Subscriber> serviceSubscribers;   // --subscriber, as parsed: the service file's
    std::vector<Department> storedDepartments;    // the window's list, as last given
    std::vector<Client> departmentSubscribers;    // those of it that are subscribed now
    struct SentTally
    {
        long long count = 0;
        double last = 0;
    };
    std::map<std::string, SentTally> sentTally;   // per client name: what Core::send sent it
    std::set<std::string> motionOwnHosts;
    MotionTarget motion;
    DeskFollowers desks;
    std::vector<std::pair<std::string, Client>> deskClients;   // by host, in the order they joined
    SaidKinds saidKinds;
    ProdjLive prodjLive;
    OwningThread subscribersOwner;
    CoreView published;
    std::optional<double> viewPublishedAt;
    bool engineOpen = false;   // Core has sent the engine `open` since its last hello

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
    TempoSource tempo;
    double tempoLookedAt = 0.0;
    std::optional<double> engineBpm;
    // The beat the desk was last told: the one that plays at the tempo (R16), not the selected one.
    std::optional<double> beatAnnounced;
    // A NaN was just put into the state (B1: a new float object, so the state differs from the last).
    bool nanArrived = false;

    std::optional<Client> stemdeckClient;
    std::optional<Endpoint> stemdeckTarget;
    HelloWatch stemdeckWatch;

    Dispatcher dispatcher;
};
}
