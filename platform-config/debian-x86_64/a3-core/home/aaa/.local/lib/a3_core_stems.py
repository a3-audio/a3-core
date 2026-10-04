"""The mirror of StemDeck's bus switches, and the desk's input selectors on it.

StemDeck owns the switches (spec stemdeck-remote, 2026-10-01); Core keeps
what it last reported, one mask per stem pair (bit b-1 = bus b: 1-4 the desk
channels, 5 AUX, 6 CUE). Core applies its commands to the mirror at once;
StemDeck's report overwrites whatever Core expected.

The desk (2026-10-04): each channel encoder is an input selector -- one
cursor over nine inputs, deck 1's stems 1-4, deck 2's stems 1-4 and A, the
channel's analog input. A push makes the input under the cursor the
channel's only one: a stem playing on another channel moves here, and A
releases every stem, so the analog input plays again. A channel plays one
stem at most, and a stem plays on one channel at most. The aux return
knows two modes: stem (every stem on no channel plays on the return) and
analog (no stem on it). tidy() makes the mirror obey these rules. Pure: no
OSC.
"""

PAIRS = 8
CHANNELS = 4
RETURN = 4
AUX = 5
CUE = 6
ALL_BUSES = (1 << CUE) - 1
STEMS_PER_DECK = 4

#: A channel selector's inputs: 0-7 the stem pairs 1-8, then ANALOG.
INPUTS = PAIRS + 1
ANALOG = PAIRS

#: The aux return's two modes; its encoder's cursor runs over both.
ANALOG_MODE, STEM_MODE = 0, 1

def _bit(bus):
    return 1 << (bus - 1)


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


class Stems:
    def __init__(self):
        self.masks = [0] * PAIRS
        self.cursors = [ANALOG] * CHANNELS
        self.return_cursor = STEM_MODE
        self.return_mode = STEM_MODE

    # -- what StemDeck says -------------------------------------------------

    def report(self, pair, mask):
        """Take StemDeck's word for one stem. False for a damaged one. The
        rules are applied later (tidy), once the reports have settled."""
        if not (_is_int(pair) and _is_int(mask) and 1 <= pair <= PAIRS
                and 0 <= mask <= ALL_BUSES):
            return False
        self.masks[pair - 1] = mask
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
            self.return_cursor = (self.return_cursor + steps) % 2
            return
        self.cursors[index] = max(0, min(ANALOG, self.cursors[index] + steps))

    def push(self, index, connected=True):
        """The commands a push on place `index` means. Without StemDeck
        (`connected` False) nothing is switched: the mirror is StemDeck's,
        and a stem nobody plays must not shut a channel's analog input."""
        if index == RETURN:
            self.return_mode = self.return_cursor
            return self.tidy() if connected else []
        if not connected:
            return []
        cursor = self.cursors[index]
        if cursor == ANALOG:
            return self._release(index) + self.tidy()
        return self._select(index, cursor + 1)

    def _release(self, index):
        bus = index + 1
        commands = [(p, bus, False) for p in range(1, PAIRS + 1) if self._on(p, bus)]
        self._apply_all(commands)
        return commands

    def _select(self, index, pair):
        """`pair` as the channel's only input; from another channel it moves."""
        bus = index + 1
        if self._on(pair, bus):
            return []
        commands = [(pair, bus, True), (pair, AUX, False)]
        commands += [(p, bus, False) for p in range(1, PAIRS + 1)
                     if p != pair and self._on(p, bus)]
        commands += [(pair, c + 1, False) for c in range(CHANNELS)
                     if c != index and self._on(pair, c + 1)]
        self._apply_all(commands)
        return commands + self.tidy()

    # -- the rules ------------------------------------------------------------

    def tidy(self):
        """The commands that make the mirror obey the rules -- one stem per
        channel bus (the lowest stays), a stem on one channel at most, and
        the return's mode -- applied at once. A second call returns []."""
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
            wanted = self.return_mode == STEM_MODE and self.place_of(pair) in (None, RETURN)
            if wanted != self._on(pair, AUX):
                commands.append((pair, AUX, wanted))
                self._apply(pair, AUX, wanted)
        return commands

    def cue_commands(self, cues):
        """StemDeck's C switches for the channel cues: a stem's C is on while
        it plays on a cued channel, off otherwise -- clicks on StemDeck's own
        C are overridden (spec desk-stem-selector)."""
        commands = []
        for pair in range(1, PAIRS + 1):
            wanted = any(cues[c] and self._on(pair, c + 1) for c in range(CHANNELS))
            if wanted != self._on(pair, CUE):
                commands.append((pair, CUE, wanted))
                self._apply(pair, CUE, wanted)
        return commands

    # -- inside ---------------------------------------------------------------

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
        raises. The switches are StemDeck's and come back with its report."""
        s = cls()
        if not isinstance(data, dict):
            return s
        cursors = _cursors(data.get("cursors"))
        if cursors is not None:
            s.cursors = cursors
        for name in ("return_cursor", "return_mode"):
            if data.get(name) in (ANALOG_MODE, STEM_MODE) and _is_int(data.get(name)):
                setattr(s, name, data[name])
        return s


def _cursors(data):
    if not isinstance(data, list) or len(data) != CHANNELS:
        return None
    if not all(_is_int(c) and 0 <= c <= ANALOG for c in data):
        return None
    return list(data)
