"""The mirror of StemDeck's bus switches, and the desk's menus on it.

StemDeck owns the switches (spec stemdeck-remote, 2026-10-01); Core keeps
what it last reported, one mask per stem pair (bit b-1 = bus b: 1-4 the desk
channels, 5 AUX, 6 CUE). Core applies its commands to the mirror at once;
StemDeck's report overwrites whatever Core expected.

The desk (spec desk-stem-grid-2, 2026-10-02): each channel encoder is a
two-level menu -- D1 / D2 / A, then a deck's stems 1-4 and back; a push on a
stem loads it as the channel's stem of that deck (one of each deck may play,
2026-10-02) or, if loaded, unloads it; a push on A releases them all. A
stem plays on one channel at most. The aux return knows two modes: stem
(every stem on no channel plays on the return) and analog (no stem on it).
tidy() makes the mirror obey both rules. Pure: no OSC.
"""

PAIRS = 8
CHANNELS = 4
RETURN = 4
AUX = 5
CUE = 6
ALL_BUSES = (1 << CUE) - 1
STEMS_PER_DECK = 4

#: A channel menu's levels, and the top level's entries.
TOP, DECK_1, DECK_2 = 0, 1, 2
D1, D2, A = 0, 1, 2
#: A deck level's entries: stems 0-3, then back.
BACK = STEMS_PER_DECK

#: The aux return's two modes; its encoder's cursor runs over both.
ANALOG_MODE, STEM_MODE = 0, 1

_ENTRIES = {TOP: 3, DECK_1: STEMS_PER_DECK + 1, DECK_2: STEMS_PER_DECK + 1}


def _bit(bus):
    return 1 << (bus - 1)


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def _deck(pair):
    """0 for StemDeck's deck 1 (pairs 1-4), 1 for deck 2 (pairs 5-8)."""
    return (pair - 1) // STEMS_PER_DECK


def pair_of_deck(level, stem):
    """The pair (1-8) of a deck level's stem entry (0-3)."""
    return (level - DECK_1) * STEMS_PER_DECK + stem + 1


class Stems:
    def __init__(self):
        self.masks = [0] * PAIRS
        self.menus = [(TOP, D1)] * CHANNELS
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

    def sources(self, index):
        """What channel `index` plays from: {D1}, {D2}, both, or {A}."""
        decks = {_deck(p) for p in range(1, PAIRS + 1) if self._on(p, index + 1)}
        return {D1 if d == 0 else D2 for d in decks} or {A}

    # -- turn and push --------------------------------------------------------

    def turn(self, index, steps):
        if index == RETURN:
            self.return_cursor = (self.return_cursor + steps) % 2
            return
        level, cursor = self.menus[index]
        self.menus[index] = (level, (cursor + steps) % _ENTRIES[level])

    def push(self, index, connected=True):
        """The commands a push on place `index` means; the menu moves too.
        Without StemDeck (`connected` False) only the menu and the return's
        mode move: the mirror is StemDeck's, and a stem nobody plays must
        not shut a channel's analog input."""
        if index == RETURN:
            self.return_mode = self.return_cursor
            return self.tidy() if connected else []
        level, cursor = self.menus[index]
        if level == TOP:
            if cursor == A:
                return self._release(index) + self.tidy() if connected else []
            self.menus[index] = (DECK_1 if cursor == D1 else DECK_2, 0)
            return []
        if cursor == BACK:
            self.menus[index] = (TOP, D1 if level == DECK_1 else D2)
            return []
        return self._load(index, pair_of_deck(level, cursor)) if connected else []

    def _release(self, index):
        bus = index + 1
        commands = [(p, bus, False) for p in range(1, PAIRS + 1) if self._on(p, bus)]
        self._apply_all(commands)
        return commands

    def _load(self, index, pair):
        """Loads `pair` as this channel's stem of its deck -- the other
        deck's stem stays -- or, if it is loaded already, unloads it."""
        place = self.place_of(pair)
        if place is not None and place not in (RETURN, index):
            return []
        bus = index + 1
        if self._on(pair, bus):
            commands = [(pair, bus, False)]
        else:
            # A stem loaded on a channel is on the return no more.
            commands = [(pair, bus, True), (pair, AUX, False)]
            commands += [(p, bus, False) for p in range(1, PAIRS + 1)
                         if p != pair and _deck(p) == _deck(pair) and self._on(p, bus)]
        self._apply_all(commands)
        return commands + self.tidy()

    # -- the rules ------------------------------------------------------------

    def tidy(self):
        """The commands that make the mirror obey the rules -- one stem of
        each deck per channel bus (the lowest stays), a stem on one channel
        at most, and
        the return's mode -- applied at once. A second call returns []."""
        commands = []
        for index in range(CHANNELS):
            bus = index + 1
            for deck in (0, 1):
                on = [p for p in range(1, PAIRS + 1) if _deck(p) == deck and self._on(p, bus)]
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
        return {"menus": [list(m) for m in self.menus],
                "return_cursor": self.return_cursor, "return_mode": self.return_mode}

    @classmethod
    def from_data(cls, data):
        """The remembered menus and return mode, or the defaults -- never
        raises. The switches are StemDeck's and come back with its report."""
        s = cls()
        if not isinstance(data, dict):
            return s
        menus = _menus(data.get("menus"))
        if menus is not None:
            s.menus = menus
        for name in ("return_cursor", "return_mode"):
            if data.get(name) in (ANALOG_MODE, STEM_MODE) and _is_int(data.get(name)):
                setattr(s, name, data[name])
        return s


def _menus(data):
    if not isinstance(data, list) or len(data) != CHANNELS:
        return None
    menus = []
    for entry in data:
        if not (isinstance(entry, list) and len(entry) == 2 and all(map(_is_int, entry))):
            return None
        level, cursor = entry
        if level not in _ENTRIES or not 0 <= cursor < _ENTRIES[level]:
            return None
        menus.append((level, cursor))
    return menus
