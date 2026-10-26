# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Core's engine-mode deck selector: each channel's input ring.

Mirrors the REAPER-mode ring (a3_core_stems) -- a turn moves a cursor, a push
commits it -- but over [ANALOG, deck 1, ..., deck D] instead of stem pairs.
Channels and decks are numbered as on the wire: channels 1-4, decks 1-D
(shown as letters A, B, ...), ANALOG = 0.

A deck sits on one channel at most: picking it on a second channel returns
the first to ANALOG. A deck that drops out stays on its channel, silent; no
automatic switch (stemdeck-bformat design, settled 6). Core sends the deck its
3D while it is on a channel, 1 (neutral) while it is on none, and again on
its next hello.

Pure: no OSC sockets, the clock is passed in. Addresses come from the truth.
"""

ANALOG = 0
#: A deck is present while its last hello is at most this many seconds old.
PRESENCE_SECONDS = 5.0
NEUTRAL_3D = 1.0

KEY = "channel.source"


def deck_port(truth, deck):
    """Where deck k listens: stemdeck.osc + (k - 1). Two one-deck StemDeck
    instances on one host cannot share a port; the base is the truth's."""
    return truth.endpoint("stemdeck", "osc")[1] + deck - 1


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


class DeckSelector:
    def __init__(self, channels=4, decks=2):
        self.channels = channels
        self.decks = decks
        self._sources = [ANALOG] * channels
        self._cursors = [0] * channels      # a place in ring(), not a source
        self._heard = {}
        self._held_before = set()

    # -- the ring ---------------------------------------------------------

    def ring(self, channel):
        return [ANALOG] + list(range(1, self.decks + 1))

    def cursor(self, channel):
        return self._cursors[channel - 1]

    def turn(self, channel, steps):
        size = len(self.ring(channel))
        self._cursors[channel - 1] = (self._cursors[channel - 1] + steps) % size

    def push(self, channel):
        """The changes (channel, source) a push causes; [] when the cursor
        is on what the channel already plays."""
        chosen = self.ring(channel)[self._cursors[channel - 1]]
        if chosen == self.source(channel):
            return []
        self._held_before = self._held()
        changes = [(channel, chosen)]
        other = self.channel_of(chosen) if chosen != ANALOG else None
        if other is not None:
            changes.append((other, ANALOG))
        for ch, source in changes:
            self._set(ch, source)
        return changes

    def _set(self, channel, source):
        self._sources[channel - 1] = source
        self._cursors[channel - 1] = self.ring(channel).index(source)

    # -- where everything is ------------------------------------------------

    def source(self, channel):
        return self._sources[channel - 1]

    def deck_channel(self, channel):
        return self.source(channel) != ANALOG

    def channel_of(self, deck):
        for channel in range(1, self.channels + 1):
            if self.source(channel) == deck:
                return channel
        return None

    # -- presence -----------------------------------------------------------

    def heard(self, deck, now):
        self._heard[deck] = now

    def present(self, deck, now):
        last = self._heard.get(deck)
        return last is not None and now - last <= PRESENCE_SECONDS

    # -- the deck's 3D ------------------------------------------------------

    def three_d_messages(self, truth, channel, value):
        deck = self.source(channel)
        if deck == ANALOG:
            return []
        return [self._three_d(truth, deck, value)]

    def leave_messages(self, truth, changes):
        """Neutral 3D for every deck the push that made `changes` took off
        its channel and put on none. A deck that only moved to another
        channel is not one: it gets that channel's 3D instead."""
        if not changes:
            return []
        gone = sorted(self._held_before - self._held())
        return [self._three_d(truth, deck, NEUTRAL_3D) for deck in gone]

    def _held(self):
        return {s for s in self._sources if s != ANALOG}

    def hello_messages(self, truth, deck, channel_3d):
        value = channel_3d if self.channel_of(deck) is not None else NEUTRAL_3D
        return [self._three_d(truth, deck, value)]

    @staticmethod
    def _three_d(truth, deck, value):
        return (truth.address("stemdeck.3d", deck=deck), value)

    # -- on disk --------------------------------------------------------------

    def to_evening(self):
        return {KEY: list(self._sources)}

    def from_evening(self, data):
        """The remembered sources; anything else -- an old stem cursor, an
        unknown or doubled deck, a wrong shape -- reads as ANALOG."""
        listed = data.get(KEY) if isinstance(data, dict) else None
        if not isinstance(listed, list) or len(listed) != self.channels:
            return
        taken = set()
        for channel, source in enumerate(listed, start=1):
            if not (_is_int(source) and 1 <= source <= self.decks) or source in taken:
                source = ANALOG
            taken.add(source)
            self._set(channel, source)
