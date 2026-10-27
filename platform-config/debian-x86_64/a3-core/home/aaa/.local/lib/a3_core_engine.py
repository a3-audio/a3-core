# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Core's words to the a3-engine, and the session that says when to replay.

In engine mode Core sends the engine the wire value as it arrived; the engine
applies the laws (zero is silence there, ruling R18). Nothing here adds one.
Pure: no sockets, no globals. Channel arguments are 0-based indexes as in
Core's channel_infos; the addresses carry the 1-based wire number.
"""

from a3_core_stems import STEM_MODE

#: The deck whose AUX pair the return plays in STEM mode.
RETURN_DECK = 1


def engine_return_source(return_mode):
    """The aux return's source from the desk's own return word (a3_core_stems'
    mode): STEM plays deck A's AUX pair, ANALOG analog 11/12."""
    return RETURN_DECK if return_mode == STEM_MODE else 0


#: Sent as they arrived, by truth name.
FORWARDED = frozenset({
    "channel.gain", "channel.eq.high", "channel.eq.mid", "channel.eq.low",
    "channel.volume", "channel.3d", "channel.filter.frequency",
    "channel.filter.q", "channel.aux-send", "filter.frequency",
    "filter.resonance", "master.volume", "master.booth", "master.phones-mix",
    "master.phones-volume", "master.aux-return",
})

#: A deck channel's own 3D and filter go to the deck or nowhere (B4), never to
#: the engine's analog path.
NOT_FOR_DECK_CHANNELS = frozenset({
    "channel.3d", "channel.filter.frequency", "channel.filter.q",
})

#: Core's name for the filter toggle is "fx"; the wire says filter.
_FLAG_KEYS = {"cue": "channel.cue", "filter": "channel.filter", "fx": "channel.filter"}


def _wire(channel_index):
    return channel_index + 1


def forward_messages(truth, key, fields, value):
    if key not in FORWARDED:
        return []
    return [(truth.address(key, **fields), float(value))]


def engine_channel_messages(truth, selector, key, fields, value):
    channel = fields.get("ch")
    if (key in NOT_FOR_DECK_CHANNELS and channel is not None
            and selector.deck_channel(channel)):
        return []
    return forward_messages(truth, key, fields, value)


def analog_again_messages(truth, channel_index, info, relayed):
    """What the engine's analog path missed while a deck held the channel: its
    3D, FREQ, Q and position, from Core's own state. `relayed` is the Relayed
    store; unknown values are left unsaid."""
    wire = _wire(channel_index)
    messages = []
    three_d = info.three_d
    if three_d is not None:
        messages.append((truth.address("channel.3d", ch=wire), float(three_d)))
    for key in ("channel.filter.frequency", "channel.filter.q"):
        address = truth.address(key, ch=wire)
        value = relayed.value(address)
        if value is not None:
            messages.append((address, float(value)))
    for word in ("azimuth", "elevation"):
        degrees = getattr(info, word)
        if degrees is not None:
            messages.append(position_message(truth, word, channel_index, degrees))
    return messages


def flag_message(truth, flag, channel_index, on):
    address = truth.address(_FLAG_KEYS[flag], ch=_wire(channel_index))
    return (address, 1.0 if on else 0.0)


def filter_mode_message(truth, mode_number):
    return (truth.address("filter.mode"), float(mode_number))


def position_message(truth, word, channel_index, degrees):
    return (truth.address(f"channel.{word}", ch=_wire(channel_index)), float(degrees))


def return_cue_message(truth, on):
    return (truth.address("aux-return.cue"), 1.0 if on else 0.0)


def tempo_message(truth, bpm):
    return (truth.address("delay.bpm"), float(bpm))


def open_message(truth):
    return (truth.address("engine.open"), 1)


def source_messages(truth, selector, return_source):
    messages = [(truth.address("channel.source", ch=channel), selector.source(channel))
                for channel in range(1, selector.channels + 1)]
    messages.append((truth.address("aux-return.source"), return_source))
    return messages


def _flag_messages(truth, channel_infos):
    for index, info in enumerate(channel_infos):
        yield flag_message(truth, "cue", index, info.toggle_cue)
        yield flag_message(truth, "filter", index, info.toggle_fx)


def _position_messages(truth, channel_infos, selector):
    for index, info in enumerate(channel_infos):
        if selector.deck_channel(_wire(index)):
            continue
        for word in ("azimuth", "elevation"):
            degrees = getattr(info, word)
            if degrees is not None:
                yield position_message(truth, word, index, degrees)


def state_messages(truth, channel_infos, master_info, selector, return_source, bpm):
    """What the engine needs that replayable(evening) does not carry."""
    messages = list(_flag_messages(truth, channel_infos))
    messages.append(filter_mode_message(truth, master_info.fx_mode.value))
    messages.append(return_cue_message(truth, master_info.return_cue))
    messages += source_messages(truth, selector, return_source)
    messages += _position_messages(truth, channel_infos, selector)
    if bpm is not None:
        messages.append(tempo_message(truth, bpm))
    return messages


class EngineSession:
    """Whether what the engine reports is the evening; the engine's twin of
    a3_core_reaper.ReaperProject, as far as a3-core.py uses it.

    A boot id not heard before is an engine that has just started and knows
    nothing: it gets the replay. The same id again is its repeated hello.
    """

    def __init__(self, evening, current):
        self._evening = list(evening)
        self._current = current
        self._boot_id = None
        self._heard_any = False
        self.live = False
        self.round = 0

    def heard_hello(self, boot_id):
        if self._heard_any and boot_id == self._boot_id:
            return False
        self._heard_any = True
        self._boot_id = boot_id
        self.round += 1
        return True

    def evening(self):
        return list(self._current()) if self.live else list(self._evening)

    def replayed(self, round_started):
        if round_started != self.round:
            return False
        self.live = True
        return True
