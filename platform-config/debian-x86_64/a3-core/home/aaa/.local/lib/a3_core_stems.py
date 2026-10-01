"""The mirror of StemDeck's bus switches, and the desk's selector on it.

StemDeck owns the switches (spec stemdeck-remote, 2026-10-01); Core keeps
what it last reported, one mask per stem pair (bit b-1 = bus b: 1-4 the desk
channels, 5 AUX, 6 CUE). The desk selects and loads (spec desk-stem-selector,
2026-10-02): five places -- the four channels and the aux return -- each with
a selection; a turn moves it over A (or the return's empty field) and the
stems that are in no other place, a push turns it into switch commands. A
stem is in one place only, and the return holds one stem at most. Core
applies its commands to the mirror at once; StemDeck's report overwrites
whatever Core expected. Pure: no OSC.
"""

PAIRS = 8
CHANNELS = 4
RETURN = 4
PLACES = 5
AUX = 5
CUE = 6
ALL_BUSES = (1 << CUE) - 1


def _bit(bus):
    return 1 << (bus - 1)


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


class Stems:
    def __init__(self):
        self.masks = [0] * PAIRS
        self.selected = [0] * PLACES

    # -- what StemDeck says -------------------------------------------------

    def report(self, pair, mask):
        """Take StemDeck's word for one stem. False for a damaged one."""
        if not (_is_int(pair) and _is_int(mask) and 1 <= pair <= PAIRS
                and 0 <= mask <= ALL_BUSES):
            return False
        self.masks[pair - 1] = mask
        self._keep_selections()
        return True

    def forget(self):
        """StemDeck is gone: nothing is on any bus."""
        self.masks = [0] * PAIRS
        self._keep_selections()

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

    def positions(self, index):
        """Where place `index`'s selection may stand: A / empty (0), then the
        stems that are nowhere else."""
        return [0] + [p for p in range(1, PAIRS + 1) if self.place_of(p) in (None, index)]

    # -- turn and push --------------------------------------------------------

    def turn(self, index, steps):
        positions = self.positions(index)
        here = positions.index(self.selected[index]) if self.selected[index] in positions else 0
        self.selected[index] = positions[(here + steps) % len(positions)]

    def push(self, index):
        """The commands that load place `index`'s selection there."""
        chosen = self.selected[index]
        bus = AUX if index == RETURN else index + 1
        commands = [(p, bus, False) for p in range(1, PAIRS + 1)
                    if p != chosen and self._on(p, bus)]
        if chosen and not self._on(chosen, bus):
            commands.insert(0, (chosen, bus, True))
            if index != RETURN:
                # A stem loaded on a channel is on the return no more.
                commands.insert(1, (chosen, AUX, False))
        for pair, b, on in commands:
            self._apply(pair, b, on)
        self._keep_selections()
        return commands

    # -- inside ---------------------------------------------------------------

    def _apply(self, pair, bus, on):
        if on:
            self.masks[pair - 1] |= _bit(bus)
        else:
            self.masks[pair - 1] &= ~_bit(bus)

    def _keep_selections(self):
        """A selection whose stem went elsewhere moves to the next position."""
        for index in range(PLACES):
            positions = self.positions(index)
            if self.selected[index] in positions:
                continue
            later = [p for p in positions if p > self.selected[index]]
            self.selected[index] = later[0] if later else 0

    # -- on disk ---------------------------------------------------------------

    def as_data(self):
        return {"selected": list(self.selected)}

    @classmethod
    def from_data(cls, data):
        """The remembered selections, or none -- never raises. The switches
        are StemDeck's and come back with its report."""
        s = cls()
        try:
            selected = [int(v) for v in data["selected"]]
        except (TypeError, KeyError, ValueError, OverflowError):
            return s
        if len(selected) == PLACES and all(0 <= v <= PAIRS for v in selected):
            s.selected = selected
        return s
