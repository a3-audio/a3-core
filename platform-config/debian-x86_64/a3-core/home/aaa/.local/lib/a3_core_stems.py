"""The mirror of StemDeck's bus switches, and the desk's input selectors on it.

StemDeck owns the switches (spec stemdeck-remote, 2026-10-01); Core keeps
what it last reported, one mask per stem pair (bit b-1 = bus b: 1-4 the desk
channels, 5 AUX). Core applies its commands to the mirror at once;
StemDeck's report overwrites whatever Core expected.

The desk (2026-10-04): each channel encoder is an input selector over three
positions on channel n: deck 1's stem n, deck 2's stem n, A (the channel's
analog input). Stem n plays only on channel n or on AUX; any other switch is
refused, and one StemDeck still reports is taken off by tidy() onto AUX.

Each channel has a source, STEM or ANALOG, and it is Core's own
(maintainer, 2026-10-14): REAPER plays the channel's input from the stems
track's send or from the analog track's send, never both, and nothing
StemDeck reports -- an Auto-DJ switch, a click in StemDeck -- changes it.
On ANALOG the channel simply does not hear bus N. The source is written
down with the cursors and replayed into REAPER after any restart.

A channel with no stored source is UNDECIDED: Core leaves its two sends in
REAPER as they are and does not guess. StemDeck's first settled report
decides it (adopt_sources, maintainer 2026-10-14): STEM where bus N has a
stem, ANALOG where it has none. A stored source always wins, and only that
first report decides; later ones never change a source.

- A push on A makes the source ANALOG and sends StemDeck nothing: the stem
  on bus N stays there and nothing moves to AUX ("A should switch to the
  analog input in REAPER, not touch StemDeck"). Until 2026-10-14 A took
  every stem off the bus, StemDeck parked it on AUX, and the analog send
  opened because the bus was empty.
- A push on a stem makes the source STEM and, as before, the stem the
  channel's only input on bus N (the other deck's stem n, if it plays here,
  goes to AUX). A push on the stem that already
  plays here changes nothing (until 2026-10-14 it released it to AUX).
  Without StemDeck a stem push changes nothing: a stem nobody plays must
  not shut a channel's analog input.
- The desk shows A while the source is ANALOG, whatever is on bus N, and
  the stem on bus N while it is STEM or UNDECIDED (A as well while that
  bus is empty -- the announcement has no "nothing").

Core remembers no stem. A channel
plays one stem at most, and a stem plays on one channel at most; every stem
on a channel is not on AUX, and a stem taken off its channel goes to AUX, in
either return mode (one stem per bus, the rest on AUX -- StemDeck's own rule
since 2026-10-07). A stem StemDeck reports on no bus at all is left there:
its Auto-DJ holds stems off the buses until their switch. The aux return
knows two modes, which only choose its source in REAPER: stem (StemDeck's AUX) and analog
(analog 11/12), so in analog mode the stems on AUX are not heard. Its cursor
runs over both and a CUE field, a ring STEM -> ANALOG -> CUE (spec
return-cue, 2026-10-04). tidy() makes the mirror obey these rules. Pure: no
OSC.
"""

PAIRS = 8
CHANNELS = 4
RETURN = 4
AUX = 5
ALL_BUSES = (1 << AUX) - 1
#: The width a report may have: a StemDeck from before 2026-10-07 still had a
#: sixth bus, its CUE, and may report it. That bit is dropped, not the report.
REPORT_BUSES = (1 << (AUX + 1)) - 1
STEMS_PER_DECK = 4

#: A channel selector's cursor: 0-7 the stem pairs 1-8 (cursor = pair - 1),
#: then A. Channel n only visits its ring(n).
ANALOG_INPUT = PAIRS

#: A channel's source (2026-10-14): which of its two sends in REAPER is open.
#: UNDECIDED (None, null on disk) until a push or StemDeck's first report.
ANALOG_SOURCE, STEM_SOURCE, UNDECIDED = 0, 1, None
SOURCES = (ANALOG_SOURCE, STEM_SOURCE)

#: The aux return's two modes; its encoder's cursor runs over both and the
#: CUE field, which is a switch of its own and never a mode (spec return-cue).
ANALOG_MODE, STEM_MODE, CUE_FIELD = 0, 1, 2

#: The return cursor's ring, in the display's order left to right.
RETURN_RING = (STEM_MODE, ANALOG_MODE, CUE_FIELD)

def ring(index):
    """Channel `index`'s selector positions, in turn order: D1 stem n, D2
    stem n, A."""
    return (index, STEMS_PER_DECK + index, ANALOG_INPUT)


def own_channel(pair):
    """The channel index 0-3 a stem (pair 1-8) may play on."""
    return (pair - 1) % STEMS_PER_DECK


def _bit(bus):
    return 1 << (bus - 1)


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


class Stems:
    def __init__(self):
        self.masks = [0] * PAIRS
        self.cursors = [ANALOG_INPUT] * CHANNELS
        self.sources = [UNDECIDED] * CHANNELS
        self.return_cursor = STEM_MODE
        self.return_mode = STEM_MODE

    # -- what StemDeck says -------------------------------------------------

    def report(self, pair, mask):
        """Take StemDeck's word for one stem. False for a damaged one. The
        rules are applied later (tidy), once the reports have settled."""
        if not (_is_int(pair) and _is_int(mask) and 1 <= pair <= PAIRS
                and 0 <= mask <= REPORT_BUSES):
            return False
        self.masks[pair - 1] = mask & ALL_BUSES
        return True

    def forget(self):
        """StemDeck is gone: nothing is on any bus."""
        self.masks = [0] * PAIRS

    # -- where everything is -------------------------------------------------

    def _on(self, pair, bus):
        return bool(self.masks[pair - 1] & _bit(bus))

    def channel_mask(self, index):
        return sum(1 << (p - 1) for p in range(1, PAIRS + 1) if self._on(p, index + 1))

    def plays_on_return(self, pair):
        return self._on(pair, AUX)

    def place_of(self, pair):
        """The channel index 0-3 a stem plays on, RETURN, or None."""
        for index in range(CHANNELS):
            if self._on(pair, index + 1):
                return index
        return RETURN if self._on(pair, AUX) else None

    def adopt_sources(self):
        """Decide every UNDECIDED channel from the buses as they are now:
        STEM where its bus has a stem, ANALOG where it has none. Called on
        StemDeck's settled report; a decided source is never touched, so
        only the first report after a start without one decides."""
        for index in range(CHANNELS):
            if self.sources[index] is UNDECIDED:
                self.sources[index] = (STEM_SOURCE if self.channel_mask(index)
                                       else ANALOG_SOURCE)

    # -- turn and push --------------------------------------------------------

    def turn(self, index, steps):
        if index == RETURN:
            place = RETURN_RING.index(self.return_cursor)
            self.return_cursor = RETURN_RING[(place + steps) % len(RETURN_RING)]
            return
        positions = ring(index)
        place = positions.index(self.cursors[index]) if self.cursors[index] in positions \
            else positions.index(ANALOG_INPUT)
        self.cursors[index] = positions[(place + steps) % len(positions)]

    def push(self, index, connected=True):
        """The commands to StemDeck a push on place `index` means. A on a
        channel only sets its source, with or without StemDeck. Without
        StemDeck (`connected` False) a stem push changes nothing: the mirror
        is StemDeck's, and a stem nobody plays must not shut a channel's
        analog input. On the return's CUE field nothing is switched: the cue
        is Core's, not StemDeck's."""
        if index == RETURN:
            if self.return_cursor == CUE_FIELD:
                return []
            self.return_mode = self.return_cursor
            return self.tidy() if connected else []
        cursor = self.cursors[index]
        if cursor not in ring(index):
            return []
        if cursor == ANALOG_INPUT:
            self.sources[index] = ANALOG_SOURCE
            return []
        if not connected:
            return []
        self.sources[index] = STEM_SOURCE
        return self._select(index, cursor + 1)

    def _select(self, index, pair):
        """`pair` as the channel's only input; a foreign stem is refused."""
        bus = index + 1
        if own_channel(pair) != index or self._on(pair, bus):
            return []
        displaced = [p for p in range(1, PAIRS + 1) if p != pair and self._on(p, bus)]
        commands = [(pair, bus, True), (pair, AUX, False)]
        commands += [(p, bus, False) for p in displaced]
        self._apply_all(commands)
        for p in displaced:
            if self.place_of(p) is None:
                commands += self._to_aux(p)
        return commands + self.tidy()

    # -- the rules ------------------------------------------------------------

    def tidy(self):
        """The commands that make the mirror obey the rules -- one stem per
        channel bus (the lowest stays), a stem only on its own channel, a
        stem on a channel not also on AUX, and a stem this takes off its
        last channel onto AUX, whatever the return's mode -- applied at
        once. A stem StemDeck reports on no bus at all stays there: its
        Auto-DJ holds the incoming deck's stems off until their switch. A
        second call returns []."""
        before = [self.place_of(p) for p in range(1, PAIRS + 1)]
        commands = [(p, c + 1, False) for p in range(1, PAIRS + 1)
                    for c in range(CHANNELS)
                    if c != own_channel(p) and self._on(p, c + 1)]
        self._apply_all(commands)
        for index in range(CHANNELS):
            bus = index + 1
            on = [p for p in range(1, PAIRS + 1) if self._on(p, bus)]
            commands += [(p, bus, False) for p in on[1:]]
        # Applied before the next rule reads the mirror: a stem taken off one
        # bus here may be alone, and right, on another.
        self._apply_all(commands)
        for pair in range(1, PAIRS + 1):
            channels = [c for c in range(CHANNELS) if self._on(pair, c + 1)]
            spare = [(pair, c + 1, False) for c in channels[1:]]
            self._apply_all(spare)
            commands += spare
        for pair in range(1, PAIRS + 1):
            place = self.place_of(pair)
            was_on_a_channel = before[pair - 1] not in (None, RETURN)
            if place is None and was_on_a_channel:
                commands += self._to_aux(pair)
            elif place not in (None, RETURN) and self._on(pair, AUX):
                commands.append((pair, AUX, False))
                self._apply(pair, AUX, False)
        return commands

    # -- inside ---------------------------------------------------------------

    def _to_aux(self, pair):
        """A stem Core takes off its channel goes to the return, as StemDeck
        sends one a click displaces."""
        self._apply(pair, AUX, True)
        return [(pair, AUX, True)]

    def _apply(self, pair, bus, on):
        if on:
            self.masks[pair - 1] |= _bit(bus)
        else:
            self.masks[pair - 1] &= ~_bit(bus)

    def _apply_all(self, commands):
        for pair, bus, on in commands:
            self._apply(pair, bus, on)

    # -- on disk ---------------------------------------------------------------

    def as_data(self):
        return {"cursors": list(self.cursors), "sources": list(self.sources),
                "return_cursor": self.return_cursor, "return_mode": self.return_mode}

    @classmethod
    def from_data(cls, data):
        """The remembered cursors, sources and return mode, or the defaults --
        never raises; an older file's "last_stems" is ignored, and one
        without "sources" leaves every channel UNDECIDED. The switches are
        StemDeck's and come back with its report."""
        s = cls()
        if not isinstance(data, dict):
            return s
        cursors = _cursors(data.get("cursors"))
        if cursors is not None:
            s.cursors = [c if c in ring(i) else ANALOG_INPUT
                         for i, c in enumerate(cursors)]
        sources = _sources(data.get("sources"))
        if sources is not None:
            s.sources = sources
        for name, allowed in (("return_cursor", RETURN_RING),
                              ("return_mode", (ANALOG_MODE, STEM_MODE))):
            if _is_int(data.get(name)) and data[name] in allowed:
                setattr(s, name, data[name])
        return s


def _sources(data):
    if not isinstance(data, list) or len(data) != CHANNELS:
        return None
    if not all(s is UNDECIDED or (_is_int(s) and s in SOURCES) for s in data):
        return None
    return list(data)


def _cursors(data):
    if not isinstance(data, list) or len(data) != CHANNELS:
        return None
    if not all(_is_int(c) and 0 <= c <= ANALOG_INPUT for c in data):
        return None
    return list(data)

