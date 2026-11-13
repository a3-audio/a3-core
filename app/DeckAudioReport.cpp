// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeckAudioReport.h"
#include "EngineHost.h"
#include "EnginePorts.h"
#include <algorithm>

namespace a3app
{
namespace
{
struct SlotLinks { bool bformat = false, stereo = false; };

// One group of ports is linked when the map names at least one peer for it and none of its peers is missing.
class GroupLinks
{
public:
    void add (const std::vector<std::string>& peers, const std::vector<std::string>& missingPeers)
    {
        for (const auto& peer : peers)
        {
            named = true;
            complete = complete && std::find (missingPeers.begin(), missingPeers.end(), peer) == missingPeers.end();
        }
    }
    bool linked() const { return named && complete; }

private:
    bool named = false, complete = true;
};

SlotLinks slotLinks (const IoMap& map, int slot, const std::vector<std::string>& missingPeers)
{
    const auto prefix = "deck" + deckLetters (slot) + "_";   // the "_" keeps deckA apart from deckAA
    const std::string stereoPorts[] = { deckStereoPortName (slot, 0), deckStereoPortName (slot, 1) };
    GroupLinks bformat, stereo;
    for (const auto& [port, peers] : map.jack)
    {
        if (port.rfind (prefix, 0) != 0)
            continue;
        const bool isStereo = port == stereoPorts[0] || port == stereoPorts[1];
        (isStereo ? stereo : bformat).add (peers, missingPeers);
    }
    return { bformat.linked(), stereo.linked() };
}

std::vector<SlotLinks> everySlot (const IoMap& map, int decks, const std::vector<std::string>& missingPeers)
{
    std::vector<SlotLinks> slots;
    for (int slot = 1; slot <= decks; ++slot)
        slots.push_back (slotLinks (map, slot, missingPeers));
    return slots;
}

// nullopt where the backend's missingPeers mean nothing yet (see deckAudioReport).
std::optional<std::vector<std::string>> plannedMissingPeers (const BackendStatus& status)
{
    if (! status.connecting || ! status.linksPlanned)
        return std::nullopt;
    std::vector<std::string> missing;
    for (const auto& peer : status.missingPeers)
        missing.push_back (peer.toStdString());
    return missing;
}
}

DeckMode deckModeOf (bool bformatLinked, bool stereoLinked)
{
    return stereoLinked && ! bformatLinked ? DeckMode::stereo : DeckMode::bformat;
}

std::vector<DeckMode> deckModes (const IoMap& map, int decks, const std::vector<std::string>& missingPeers)
{
    std::vector<DeckMode> modes;
    for (const auto& slot : everySlot (map, decks, missingPeers))
        modes.push_back (deckModeOf (slot.bformat, slot.stereo));
    return modes;
}

void applyDeckModes (const BackendStatus& status, const IoMap& map, int decks, a3::EngineControls& controls)
{
    const auto missing = plannedMissingPeers (status);
    if (! missing)
        return;
    const auto modes = deckModes (map, decks, *missing);
    for (size_t index = 0; index < modes.size(); ++index)
        controls.setDeckStereo ((int) index + 1, modes[index] == DeckMode::stereo);
}

void reportDeckModes (const BackendStatus& status, const IoMap& map, int decks, EngineHost& host)
{
    const auto missing = plannedMissingPeers (status);
    if (! missing)
        return;
    std::vector<bool> stereo;
    for (const auto mode : deckModes (map, decks, *missing))
        stereo.push_back (mode == DeckMode::stereo);
    host.setDeckStereo (stereo);
}

std::vector<bool> deckAudioLinked (const IoMap& map, int decks, const std::vector<std::string>& missingPeers)
{
    std::vector<bool> linked;
    for (const auto& slot : everySlot (map, decks, missingPeers))
        linked.push_back (slot.bformat || slot.stereo);
    return linked;
}

std::optional<std::vector<bool>> deckAudioReport (const BackendStatus& status, const IoMap& map, int decks)
{
    const auto missing = plannedMissingPeers (status);
    if (! missing)
        return std::nullopt;
    return deckAudioLinked (map, decks, *missing);
}

void reportDeckAudio (const BackendStatus& status, const IoMap& map, int decks, EngineHost& host)
{
    if (const auto report = deckAudioReport (status, map, decks))
        host.setDeckAudio (*report);
}
}
