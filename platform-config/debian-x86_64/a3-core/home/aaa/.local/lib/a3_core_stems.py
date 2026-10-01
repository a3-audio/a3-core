"""The mirror of StemDeck's bus switches, and the desk's rules on it.

StemDeck owns the switches (spec stemdeck-remote, 2026-10-01); Core keeps
what it last reported, one mask per stem pair (bit b-1 = bus b: 1-4 the desk
channels, 5 AUX, 6 CUE). A turn or push becomes switch commands; Core applies
them to the mirror at once, so the next click steps from the right place, and
StemDeck's report overwrites whatever Core expected. Pure: no OSC.
"""

PAIRS = 8
CHANNELS = 4
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
        self.return_cursor = 0
        self._keep_cursor_on_a_free_pair()

    # -- what StemDeck says -------------------------------------------------

    def report(self, pair, mask):
        """Take StemDeck's word for one stem. False for a damaged one."""
        if not (_is_int(pair) and _is_int(mask) and 1 <= pair <= PAIRS
                and 0 <= mask <= ALL_BUSES):
            return False
        self.masks[pair - 1] = mask
        self._keep_cursor_on_a_free_pair()
        return True

    def forget(self):
        """StemDeck is gone: nothing is on any bus."""
        self.masks = [0] * PAIRS
        self._keep_cursor_on_a_free_pair()

    # -- what the desk shows ------------------------------------------------

    def _on(self, pair, bus):
        return bool(self.masks[pair - 1] & _bit(bus))

    def channel_mask(self, index):
        return sum(1 << (p - 1) for p in range(1, PAIRS + 1) if self._on(p, index + 1))

    def plays_on_return(self, pair):
        return self._on(pair, AUX)

    def any_cued(self):
        return any(self._on(p, CUE) for p in range(1, PAIRS + 1))

    def _on_a_channel(self, pair, except_index=None):
        return any(self._on(pair, c + 1) for c in range(CHANNELS) if c != except_index)

    def free_pairs(self):
        return [p for p in range(1, PAIRS + 1) if not self._on_a_channel(p)]

    # -- a channel's encoder ------------------------------------------------

    def turn_channel(self, index, steps):
        """Step channel `index` through A and the stems on no other channel;
        the commands that make StemDeck play exactly the new one there."""
        bus = index + 1
        held = [p for p in range(1, PAIRS + 1) if self._on(p, bus)]
        positions = [0] + [p for p in range(1, PAIRS + 1)
                           if not self._on_a_channel(p, except_index=index)]
        here = positions.index(held[0]) if held and held[0] in positions else 0
        after = positions[(here + steps) % len(positions)]
        commands = []
        if after:
            commands += [(after, bus, True), (after, AUX, False)]
        for pair in held:
            if pair == after:
                continue
            commands.append((pair, bus, False))
            self._apply(pair, bus, False)
            if not self._on_a_channel(pair):
                commands.append((pair, AUX, True))
        for pair, b, on in commands:
            self._apply(pair, b, on)
        self._keep_cursor_on_a_free_pair()
        return commands

    # -- the aux return's encoder -------------------------------------------

    def turn_return(self, steps):
        free = self.free_pairs()
        if not free:
            self.return_cursor = 0
            return
        here = free.index(self.return_cursor) if self.return_cursor in free else -1
        self.return_cursor = free[(here + steps) % len(free)]

    def push_return(self):
        """The command that toggles AUX of the stem under the cursor."""
        if self.return_cursor not in self.free_pairs():
            return []
        pair = self.return_cursor
        command = (pair, AUX, not self.plays_on_return(pair))
        self._apply(*command)
        return [command]

    # -- inside -----------------------------------------------------------------

    def _apply(self, pair, bus, on):
        if on:
            self.masks[pair - 1] |= _bit(bus)
        else:
            self.masks[pair - 1] &= ~_bit(bus)

    def _keep_cursor_on_a_free_pair(self):
        free = self.free_pairs()
        if self.return_cursor in free:
            return
        later = [p for p in free if p > self.return_cursor]
        self.return_cursor = later[0] if later else (free[0] if free else 0)

    # -- on disk ------------------------------------------------------------------

    def as_data(self):
        return {"return_cursor": self.return_cursor}

    @classmethod
    def from_data(cls, data):
        """The remembered cursor, or a fresh one -- never raises. The switches
        are StemDeck's and come back with its report."""
        s = cls()
        try:
            cursor = int(data["return_cursor"])
        except (TypeError, KeyError, ValueError, OverflowError):
            return s
        if 0 <= cursor <= PAIRS:
            s.return_cursor = cursor
        return s
