"""The mirror of StemDeck's bus switches, and the desk's selector on it.

StemDeck owns the switches (spec stemdeck-remote, 2026-10-01); Core keeps
what it last reported, one mask per stem pair (bit b-1 = bus b: 1-4 the desk
channels, 5 AUX, 6 CUE). The desk selects and loads (spec desk-stem-selector,
2026-10-02): five places -- the four channels and the aux return -- each with
a selection, and a push turns it into switch commands. A stem plays on one
channel or on the return, never on two channels (spec desk-stem-grid,
2026-10-02): a channel turns over A and the stems on no other channel -- one
on the return included, and loading it takes it off the return -- and the
return turns over the stems on no channel and toggles them, so it may hold
several. Core
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
        """Where place `index`'s selection may stand. A channel: A (0), then
        the stems on no other channel. The return: the stems on no channel,
        or 0 when there is none."""
        if index == RETURN:
            free = [p for p in range(1, PAIRS + 1) if self.place_of(p) in (None, RETURN)]
            return free or [0]
        return [0] + [p for p in range(1, PAIRS + 1)
                      if self.place_of(p) in (None, RETURN, index)]

    # -- turn and push --------------------------------------------------------

    def turn(self, index, steps):
        positions = self.positions(index)
        here = positions.index(self.selected[index]) if self.selected[index] in positions else 0
        self.selected[index] = positions[(here + steps) % len(positions)]

    def push(self, index):
        """The commands that load place `index`'s selection there -- on the
        return, that switch its selected stem on or off."""
        chosen = self.selected[index]
        if index == RETURN:
            return self._toggle_on_return(chosen)
        bus = index + 1
        commands = [(p, bus, False) for p in range(1, PAIRS + 1)
                    if p != chosen and self._on(p, bus)]
        if chosen and not self._on(chosen, bus):
            # A stem loaded on a channel is on the return no more.
            commands[:0] = [(chosen, bus, True), (chosen, AUX, False)]
        for pair, b, on in commands:
            self._apply(pair, b, on)
        self._keep_selections()
        return commands

    def _toggle_on_return(self, chosen):
        if not chosen:
            return []
        on = not self._on(chosen, AUX)
        self._apply(chosen, AUX, on)
        self._keep_selections()
        return [(chosen, AUX, on)]

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

    def _keep_selections(self):
        """A selection whose stem went elsewhere moves to the next position."""
        for index in range(PLACES):
            positions = self.positions(index)
            if self.selected[index] in positions:
                continue
            later = [p for p in positions if p > self.selected[index]]
            self.selected[index] = later[0] if later else positions[0]

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
