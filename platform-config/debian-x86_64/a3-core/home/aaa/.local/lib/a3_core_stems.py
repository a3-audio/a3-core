"""Stems on the desk: which StemDeck pair plays on which channel.

Eight stereo pairs (StemDeck's 16 outputs), four desk channels. A channel
holds one pair or none (0); turning its encoder steps through none and the
pairs no other channel holds. The FX return lists the pairs on no channel,
and its push mutes or unmutes the one it shows. A pair on a channel is
always muted on the return; releasing it there unmutes it again (decided
2026-10-01). Pure: no OSC, no REAPER -- a3-core.py turns the state into
messages through a3_core_stems_reaper.
"""

PAIRS = 8
CHANNELS = 4


class Stems:
    def __init__(self, pairs=PAIRS, channels=CHANNELS):
        self.pairs = pairs
        self.channel_pair = [0] * channels
        self.return_cursor = 0
        self.return_muted = [False] * pairs

    # -- channels ---------------------------------------------------------

    def _positions_for(self, index):
        """none, then the pairs not held by another channel, in order."""
        held = {p for i, p in enumerate(self.channel_pair) if i != index and p}
        return [0] + [p for p in range(1, self.pairs + 1) if p not in held]

    def turn_channel(self, index, steps):
        positions = self._positions_for(index)
        here = positions.index(self.channel_pair[index]) \
            if self.channel_pair[index] in positions else 0
        before = self.channel_pair[index]
        after = positions[(here + steps) % len(positions)]
        self.channel_pair[index] = after
        if before and before != after:
            self.return_muted[before - 1] = False      # released: unmuted
        if self.return_cursor == after and after:
            self._move_cursor_to_a_free_pair()

    # -- the return -------------------------------------------------------

    def free_pairs(self):
        held = set(self.channel_pair)
        return [p for p in range(1, self.pairs + 1) if p not in held]

    def _move_cursor_to_a_free_pair(self):
        free = self.free_pairs()
        if not free:
            self.return_cursor = 0
            return
        later = [p for p in free if p > self.return_cursor]
        self.return_cursor = later[0] if later else free[0]

    def turn_return(self, steps):
        free = self.free_pairs()
        if not free:
            self.return_cursor = 0
            return
        here = free.index(self.return_cursor) if self.return_cursor in free else -1
        self.return_cursor = free[(here + steps) % len(free)]

    def push_return(self):
        if self.return_cursor in self.free_pairs():
            i = self.return_cursor - 1
            self.return_muted[i] = not self.return_muted[i]

    def muted_on_return(self, pair):
        """Whether `pair` is silent on the FX return."""
        return pair in self.channel_pair or self.return_muted[pair - 1]

    # -- on disk ------------------------------------------------------------

    def as_data(self):
        return {"channel_pair": list(self.channel_pair),
                "return_cursor": self.return_cursor,
                "return_muted": list(self.return_muted)}

    @classmethod
    def from_data(cls, data, pairs=PAIRS, channels=CHANNELS):
        """A remembered state, or all none -- never raises."""
        s = cls(pairs, channels)
        try:
            chan = [int(p) for p in data["channel_pair"]]
            taken = [p for p in chan if p]
            if (len(chan) == channels and all(0 <= p <= pairs for p in chan)
                    and len(taken) == len(set(taken))):
                s.channel_pair = chan
            muted = [bool(m) for m in data.get("return_muted", [])]
            if len(muted) == pairs:
                s.return_muted = [m and (i + 1) not in s.channel_pair
                                  for i, m in enumerate(muted)]
            cursor = int(data.get("return_cursor", 0))
            s.return_cursor = cursor if cursor in s.free_pairs() else 0
        except (TypeError, KeyError, ValueError, AttributeError):
            return cls(pairs, channels)
        return s
