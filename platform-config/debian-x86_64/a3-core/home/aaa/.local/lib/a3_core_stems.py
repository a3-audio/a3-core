"""The mirror of StemDeck's bus switches, and the desk's input selectors on it.

StemDeck owns the switches (spec stemdeck-remote, 2026-10-01); Core keeps
what it last reported, one mask per stem pair (bit b-1 = bus b: 1-4 the desk
channels, 5 AUX). Core applies its commands to the mirror at once;
StemDeck's report overwrites whatever Core expected.

The desk (2026-10-04): each channel encoder is an input selector -- one
cursor over nine positions, deck 1's stems 1-4, deck 2's stems 1-4 and A,
the channel's analog input. A push on a stem makes it the channel's only
input (a stem playing on another channel moves here); a push on the stem
that already plays here releases it. A push on A only ever selects analog
(2026-10-07; until then a STEM toggle that brought the last stem back):
while a stem plays every stem leaves the channel, while analog plays
nothing changes. Core remembers no stem. A channel
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

#: A channel selector's positions: 0-7 the stem pairs 1-8, then A.
INPUTS = PAIRS + 1
ANALOG_INPUT = PAIRS

#: The aux return's two modes; its encoder's cursor runs over both and the
#: CUE field, which is a switch of its own and never a mode (spec return-cue).
ANALOG_MODE, STEM_MODE, CUE_FIELD = 0, 1, 2

#: The return cursor's ring, in the display's order left to right.
RETURN_RING = (STEM_MODE, ANALOG_MODE, CUE_FIELD)

def _bit(bus):
    return 1 << (bus - 1)


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


class Stems:
    def __init__(self):
        self.masks = [0] * PAIRS
        self.cursors = [ANALOG_INPUT] * CHANNELS
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

    # -- turn and push --------------------------------------------------------

    def turn(self, index, steps):
        if index == RETURN:
            place = RETURN_RING.index(self.return_cursor)
            self.return_cursor = RETURN_RING[(place + steps) % len(RETURN_RING)]
            return
        # Round in a ring: left from stem 1 is A, one turn away.
        self.cursors[index] = (self.cursors[index] + steps) % INPUTS

    def push(self, index, connected=True):
        """The commands a push on place `index` means. Without StemDeck
        (`connected` False) nothing is switched: the mirror is StemDeck's,
        and a stem nobody plays must not shut a channel's analog input.
        On the return's CUE field nothing is switched: the cue is Core's,
        not StemDeck's."""
        if index == RETURN:
            if self.return_cursor == CUE_FIELD:
                return []
            self.return_mode = self.return_cursor
            return self.tidy() if connected else []
        if not connected:
            return []
        cursor = self.cursors[index]
        # On A, or on the stem that plays here: analog; the released stem
        # goes to AUX.
        if cursor == ANALOG_INPUT or self._on(cursor + 1, index + 1):
            return self._select_analog(index)
        return self._select(index, cursor + 1)

    def _select_analog(self, index):
        """Every stem off the channel; with none on it, nothing to do."""
        bus = index + 1
        released = [p for p in range(1, PAIRS + 1) if self._on(p, bus)]
        if not released:
            return []
        commands = [(p, bus, False) for p in released]
        self._apply_all(commands)
        for pair in released:
            if self.place_of(pair) is None:
                commands += self._to_aux(pair)
        return commands + self.tidy()

    def _select(self, index, pair):
        """`pair` as the channel's only input; from another channel it moves."""
        bus = index + 1
        if self._on(pair, bus):
            return []
        displaced = [p for p in range(1, PAIRS + 1) if p != pair and self._on(p, bus)]
        commands = [(pair, bus, True), (pair, AUX, False)]
        commands += [(p, bus, False) for p in displaced]
        commands += [(pair, c + 1, False) for c in range(CHANNELS)
                     if c != index and self._on(pair, c + 1)]
        self._apply_all(commands)
        for p in displaced:
            if self.place_of(p) is None:
                commands += self._to_aux(p)
        return commands + self.tidy()

    # -- the rules ------------------------------------------------------------

    def tidy(self):
        """The commands that make the mirror obey the rules -- one stem per
        channel bus (the lowest stays), a stem on one channel at most, a
        stem on a channel not also on AUX, and a stem this takes off its
        last channel onto AUX, whatever the return's mode -- applied at
        once. A stem StemDeck reports on no bus at all stays there: its
        Auto-DJ holds the incoming deck's stems off until their switch. A
        second call returns []."""
        before = [self.place_of(p) for p in range(1, PAIRS + 1)]
        commands = []
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
        return {"cursors": list(self.cursors),
                "return_cursor": self.return_cursor, "return_mode": self.return_mode}

    @classmethod
    def from_data(cls, data):
        """The remembered cursors and return mode, or the defaults -- never
        raises; an older file's "last_stems" is ignored. The switches are StemDeck's and come back with its report."""
        s = cls()
        if not isinstance(data, dict):
            return s
        cursors = _cursors(data.get("cursors"))
        if cursors is not None:
            s.cursors = cursors
        for name, allowed in (("return_cursor", RETURN_RING),
                              ("return_mode", (ANALOG_MODE, STEM_MODE))):
            if _is_int(data.get(name)) and data[name] in allowed:
                setattr(s, name, data[name])
        return s


def _cursors(data):
    if not isinstance(data, list) or len(data) != CHANNELS:
        return None
    if not all(_is_int(c) and 0 <= c <= ANALOG_INPUT for c in data):
        return None
    return list(data)

