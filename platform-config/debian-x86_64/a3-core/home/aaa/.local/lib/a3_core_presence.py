"""Whether a device that says hello is still there.

StemDeck says hello every 30 s; Core counts it gone after a minute without
one (spec stemdeck-remote); Motion the same since 2026-10-05
(spec devices-and-remote-access). Pure: the caller passes the clock.
"""

STEMDECK_SILENCE = 60.0
MOTION_SILENCE = 60.0


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


class HelloWatch:
    """What a device's hello means for Core's link to it -- StemDeck's and
    Motion's alike, one of each at a time.

    A hello that is news -- the first, one after silence, or one from a host
    that has just arrived -- means: send to that host from now on (and ask it
    for everything). A minute without one from the host followed means:
    forget it, once. Pure; the caller passes the clock and does the sending.

    Each host is watched on its own, so a host already there that keeps
    saying hello every 30 s is not an arrival: two running at once would
    otherwise take the link from each other on every hello.
    """

    def __init__(self, silence_after):
        self._silence_after = silence_after
        self._heard = {}
        self.host = None

    def hello(self, host, now):
        """True when Core should (re)connect to `host` and send a recall."""
        self._forget_the_silent(now)
        presence = self._heard.setdefault(host, Presence(self._silence_after))
        arrived = presence.heard(now)
        if not arrived and self.host is not None:
            return False
        self.host = host
        return True

    def silence(self, now):
        """True exactly once when the host followed has gone quiet."""
        if self.host is None or self._heard[self.host].present(now):
            return False
        self.host = None
        return True

    def _forget_the_silent(self, now):
        for host in [h for h, p in self._heard.items()
                     if h != self.host and not p.present(now)]:
            del self._heard[host]
