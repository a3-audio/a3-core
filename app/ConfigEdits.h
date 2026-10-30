// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ConfigFile.h"
#include "EnginePorts.h"
#include "SpatialConfig.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace a3app
{
// The pages' edits, pure: each returns a config that loadAppConfig would accept, or nullopt
// with the reason. An edit never leaves an I/O entry naming a port the new config lacks.

// The engine's ports for this config (loads its spatial directory).
std::optional<PortNames> portNamesFor (const AppConfig&, juce::String& error);
// The map without the entries that name a port `ports` lacks. Direct links are foreign names
// on both ends and stay.
IoMap prunedTo (const IoMap&, const PortNames& ports);

constexpr int maxChannelsOnPage = 128;
// N channels: the analog pair list follows (a new channel takes the next free pair below the
// return, else it is deck-only).
std::optional<AppConfig> withChannels (const AppConfig&, int channels, juce::String& error);
// The page's bound only (A17): one letter per slot, A..Z. The engine itself keeps no cap (A8).
constexpr int maxDecksOnPage = 26;
// N decks, named in the file from then on: a removed slot's links leave the map, an added slot the
// map names none of gets its local StemDeck's links (A9); the other slots stay as they are.
std::optional<AppConfig> withDecks (const AppConfig&, int decks, juce::String& error);
// Both zones from `directory`.
std::optional<AppConfig> withSpatialDirectory (const AppConfig&, const juce::File& directory, juce::String& error);
// One zone's variant; the other zone keeps the one it has (MAIN's also carries the shared files, ConfigFile.h).
std::optional<AppConfig> withZoneSpatialDirectory (const AppConfig&, a3::SpeakerZone, const juce::File& directory, juce::String& error);

// The one edit that writes files (the SPEAKERS page's SAVE TO ZONE): a new user variant, zoneVariantDirectory,
// holding a copy of the zone's spatial directory's files with the zone's <zone>.decoder.json (verbatim) and
// <zone>.parameters.json replaced and its <zone>.zone.json topDirections taken from the decoder's layout. It is
// staged in <userRoot>/.staging, moved into place only once it loads, and chosen as the spatial directory, so
// Save restarts the engine on it. It is chosen for that zone only. No directory is ever rewritten or deleted: the previous one stays the way
// back. A refusal names the file and leaves nothing behind.
std::optional<AppConfig> withZoneDecoder (const AppConfig&, a3::SpeakerZone, const juce::String& decoderJson,
                                          const juce::var& parameters, const juce::File& userRoot, juce::String& error,
                                          juce::Time now = juce::Time::getCurrentTime());
// <userRoot>/<base>-YYYYMMDD-HHMMSS, base being the zone's directory name without an earlier stamp; "-2", "-3"
// ... when that exists.
juce::File zoneVariantDirectory (const AppConfig&, const juce::File& userRoot, juce::Time now,
                                 a3::SpeakerZone = a3::SpeakerZone::main);

// The user's variants (the ZONES page's RENAME and DELETE). Only a direct, visible child directory of `userRoot`
// (~/.config/a3/engine/spatial) is one; installed directories never are.
bool isUserVariant (const juce::File& directory, const juce::File& userRoot);
bool usesVariant (const AppConfig&, const juce::File& directory);   // either zone
// Why `directory` cannot be deleted ("" when it can): not a user variant, or used by what runs or the pending edit.
juce::String deleteRefusal (const juce::File& directory, const juce::File& userRoot, const AppConfig& running, const AppConfig& pending);
juce::Result deleteVariant (const juce::File& directory, const juce::File& userRoot, const AppConfig& running, const AppConfig& pending);
// Why `directory` cannot be renamed to `name` ("" when it can): not a user variant, used by what runs (its saved
// file names it), or a name with a slash, a leading dot, blank, or one that exists.
juce::String renameRefusal (const juce::File& directory, const juce::String& name, const juce::File& userRoot, const AppConfig& running);
// The same without the name: whether `directory` can be renamed at all.
juce::String renameRefusal (const juce::File& directory, const juce::File& userRoot, const AppConfig& running);
std::optional<juce::File> renameVariant (const juce::File& directory, const juce::String& name, const juce::File& userRoot,
                                         const AppConfig& running, juce::String& error);
// `config` with every zone that named `from` naming `to`.
AppConfig withRenamedVariant (const AppConfig&, const juce::File& from, const juce::File& to);
// The digital loop or the analog test pair differ (2026-10-31: a Save that only picked the loop was not applied, so the
// running engine kept "no loop configured"). sameConfig sees it; the engine measures after such an apply.
bool measurementPairsChanged (const AppConfig&, const AppConfig&);

enum class TrimZone { main, booth, phones, rec };
std::optional<AppConfig> withTrim (const AppConfig&, TrimZone, float db, juce::String& error);   // refuses outside -90..+6 dB

// "off" or one of a3::headphoneEqModels(); anything else is refused naming it.
std::optional<AppConfig> withHeadphoneEq (const AppConfig&, const juce::String& model, juce::String& error);

// Where a deck slot's 41 inputs come from: its local StemDeck, or the AES67 stream of that deck.
enum class DeckSource { local, aes67 };
// Replaces exactly the slot's 41 entries (deck{X}_1..16, _aux_1..16, _cue_1..9) with literal links;
// every other entry stays.
void setDeckSource (IoMap&, int slotOneBased, DeckSource);
// Each slot 1..decks the map names none of the 41 ports of (no JACK peer, no device channel) gets its
// local StemDeck's links; a slot it names any port of stays as it is.
void linkUnnamedSlotsLocally (IoMap&, int decks);
void linkUnnamedSlotLocally (IoMap&, int slotOneBased);
// The source whose 41 links the slot holds exactly; nullopt when it holds anything else
// (missing, partial, or hand-edited links).
std::optional<DeckSource> deckSourceOf (const IoMap&, int slotOneBased);

bool sameConfig (const AppConfig&, const AppConfig&);

// One engine port as the I/O page shows it. `mapped` is what the config holds (editable);
// `foreign` are live peers the config does not hold (shown, never edited).
struct IoRow
{
    std::string port;
    bool input = false;
    std::vector<std::string> mapped, foreign;
    std::optional<int> deviceChannel;
};
using LiveLinks = std::vector<std::pair<std::string, std::string>>;   // (from, to), as AudioBackend::liveLinks
std::vector<IoRow> ioRows (const AppConfig&, const PortNames&, const LiveLinks&);

// The I/O page's OUTPUT: the engine's outputs of MAIN, BOOTH (each its sub, then its tops: the zone's layout),
// PHONES and REC, in that order. The channels' pre/post meter outputs belong to no zone.
struct ZoneOutputs
{
    juce::String zone;
    std::vector<std::string> ports;
};
std::vector<ZoneOutputs> zoneOutputGroups (const PortNames&);
// The I/O page's INPUT: per channel the analog inputs of its pair (analogPairs); none for a deck-only channel.
struct ChannelInputs
{
    int channel = 0;
    std::optional<std::string> left, right;
};
std::vector<ChannelInputs> channelInputPorts (const a3::EngineConfig&);
// One route for an engine port, the dropdowns' edit: `peer` replaces every JACK peer the map holds for it
// (empty: none); a device channel (0-based; nullopt: none) likewise. Live foreign links are not the map's.
AppConfig withJackRoute (const AppConfig&, const std::string& port, const std::string& peer);
AppConfig withDeviceRoute (const AppConfig&, const std::string& port, std::optional<int> channel);
// One choice of a route dropdown: what it reads, and the JACK peer or the device channel (0-based) it routes to.
struct RouteChoice
{
    juce::String label;
    std::string peer;
    std::optional<int> channel;
};
// The I/O page's choices for an input (capture side) or an output: JACK: `hardwarePorts` (the backend's physical
// capture or playback ports); a device: its channels 1..deviceChannels. The round-trip loop's are left out.
// A JACK port's label is peerLabel's. `deviceName`: BackendStatus::deviceName.
std::vector<RouteChoice> routeChoices (const AppConfig&, const juce::StringArray& hardwarePorts, int deviceChannels, bool input,
                                       const juce::String& deviceName);
// How the I/O page names a JACK peer: a device channel by the device ("Scarlett 18i20 USB out 1"; "Device" when the
// name is empty), anything else by its port name.
juce::String peerLabel (const std::string& port, const juce::String& deviceName);
// Why a route the editor was given cannot be taken ("" when it can): a peer, or a device channel (0-based) of
// an input or an output, that is the round-trip loop's.
juce::String loopRefusal (const AppConfig&, const std::vector<std::string>& peers);
juce::String loopRefusal (const AppConfig&, bool input, int channel0);
// The I/O page's notice: which channels the loop holds, and the routes a load left out because they were on
// it (AppConfig::loopDropped). Empty without a loop.
juce::String loopNotice (const AppConfig&, const std::vector<std::string>& loopDropped);

// The 1-based channels a side offers for the analog test pair: JACK: those of the hardware ports (RoundTripLoop.h
// deviceChannelOfPeer); a device: 1..deviceChannels. The digital loop's are left out.
std::vector<int> analogTestChannels (const AppConfig&, const juce::StringArray& hardwarePorts, int deviceChannels, bool input);
// One side of the analog test pair (0: none). The stored converters' share stays (it is the hardware's, not the
// cable's); with neither side nor a share there is no analog loop.
AppConfig withAnalogTestChannel (const AppConfig&, bool input, int channel);

// The 1-based channels a side offers for the digital loop: every one of the hardware (JACK) or device. One in use is
// refused by withDigitalLoopChannel, which names who uses it.
std::vector<int> digitalLoopChannels (const AppConfig&, const juce::StringArray& hardwarePorts, int deviceChannels, bool input);
// One side of the digital loop (the I/O page): 0 is no loop at all. Without a loop, a channel sets both sides (out N
// -> in N, as an S/PDIF cable runs); the loop keeps its channel count. Refused, naming the channel and its user, when
// a channel the loop would reserve carries a route or the analog test pair (the reservation is never taken from them).
std::optional<AppConfig> withDigitalLoopChannel (const AppConfig&, bool input, int channel, juce::String& error);

// The engine ports other than `except` the map routes to `peer` (a JACK peer, or "#<n>" for device channel n, 0-based).
std::vector<std::string> portsRoutedTo (const AppConfig&, const std::string& peer, const std::string& except);
}
