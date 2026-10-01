"""Whether a device that says hello is still there.

StemDeck says hello every 30 s; Core counts it gone after a minute without
one (spec stemdeck-remote). Pure: the caller passes the clock.
"""

STEMDECK_SILENCE = 60.0


class Presence:
    def __init__(self, silence_after):
        self._silence_after = silence_after
        self._last = None
        self._counted_gone = False

    def present(self, now):
        return self._last is not None and now - self._last <= self._silence_after

    def heard(self, now):
        """Note a hello. True if it is news: the first, or after silence."""
        news = not self.present(now)
        self._last = now
        self._counted_gone = False
        return news

    def gone(self, now):
        """True exactly once, when the silence has begun."""
        if self._last is None or self._counted_gone or self.present(now):
            return False
        self._counted_gone = True
        return True


class StemDeckWatch:
    """What StemDeck's hello means for Core's link to it.

    A hello that is news -- the first, one after silence, or one from a new
    host -- means: send to that host from now on and ask for every switch.
    A minute without one means: forget it, once. Pure; the caller passes
    the clock and does the sending."""

    def __init__(self, silence_after):
        self._presence = Presence(silence_after)
        self.host = None

    def hello(self, host, now):
        """True when Core should (re)connect to `host` and send a recall."""
        news = self._presence.heard(now)
        if not news and host == self.host:
            return False
        self.host = host
        return True

    def silence(self, now):
        """True exactly once when StemDeck has gone quiet."""
        if not self._presence.gone(now):
            return False
        self.host = None
        return True
