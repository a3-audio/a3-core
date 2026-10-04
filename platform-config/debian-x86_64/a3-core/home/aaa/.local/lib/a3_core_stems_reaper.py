"""The stem mirror as messages: the analog sends to REAPER, the desk's
announcements, and switch commands to StemDeck (spec stemdeck-remote).

Send volume, not send mute: REAPER's OSC has no send mute
(Default.ReaperOSC, 2026-10-01).
"""

from a3_core_stems import CHANNELS, PAIRS, STEMS_PER_DECK



def deck_and_stem(pair):
    return (pair - 1) // STEMS_PER_DECK + 1, (pair - 1) % STEMS_PER_DECK + 1


def pair_of(deck, stem):
    return (deck - 1) * STEMS_PER_DECK + stem


def analog_messages(stems, layout, unity):
    """Channel N's analog input: the analog track's send to N-input, shut
    while StemDeck has a stem on bus N."""
    track = layout.master.track_analog
    return [(layout.address("track_send", track=track, send=layout.channel(i).analog_send),
             0.0 if stems.channel_mask(i) else unity)
            for i in range(CHANNELS)]


def announcements(stems, truth):
    """Per channel what plays there and where its selector's cursor stands,
    then the return's cursor and what plays on it, and its mode."""
    out = []
    for c in range(CHANNELS):
        out.append((truth.address("channel.stem", ch=c + 1), stems.channel_mask(c)))
        out.append((truth.address("channel.stem.cursor", ch=c + 1), stems.cursors[c]))
    plays = [int(stems.plays_on_return(p)) for p in range(1, PAIRS + 1)]
    out.append((truth.address("aux-return.stem"), [stems.return_cursor] + plays))
    out.append((truth.address("aux-return.stem.mode"), stems.return_mode))
    return out


class Settle:
    """True once when `delay` seconds have passed since the last poke
    (spec desk-stem-grid-2): StemDeck reports stem by stem, and Core tidies
    only the settled picture. The caller passes the clock."""

    def __init__(self, delay):
        self._delay = delay
        self._last = None

    def poke(self, now):
        self._last = now

    def due(self, now):
        if self._last is None or now - self._last < self._delay:
            return False
        self._last = None
        return True


def command_messages(commands, truth):
    out = []
    for pair, bus, on in commands:
        deck, stem = deck_and_stem(pair)
        out.append((truth.address("stemdeck.bus", deck=deck, stem=stem, bus=bus), int(on)))
    return out


def changed_messages(messages, last_sent):
    """The messages whose value differs from what was last sent, in order.

    `last_sent` is the caller's memory ({address: value}); it is updated with
    the returned pairs, so a second call with the same messages returns [].
    Clear the dict to force a full send."""
    changed = [(address, value) for address, value in messages
               if last_sent.get(address) != value]
    last_sent.update(changed)
    return changed
