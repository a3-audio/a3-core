"""Where this system's OSC addresses and its REAPER track numbers live.

Both used to be literals in a3-core.py: forty-five address templates and four
records of eight track numbers each. The track numbers are the map between an
A3 channel and the REAPER project -- change the project and they have to
follow, and nothing about them said so.

They ship with the package, beside the REAPER project they must agree with,
and an update replaces them. That is deliberate: a track number that no longer
matches the shipped project is worse than a lost local edit. (Unlike the files
under /etc -- see issues/a3-core-update-ueberschreibt-lokale-konfiguration.md.)

No pythonosc, no sockets, nothing that starts a server: this module is
importable on any machine, which is what makes it testable at all. The same
reasoning tools/curve-characterisation/ already follows.
"""

import json
from pathlib import Path

#: Every track number a channel has to name. A record short of one of these is
#: refused rather than defaulted -- a missing number would read as track 0 and
#: quietly drive whatever REAPER happens to have there.
CHANNEL_FIELDS = (
    "track_input",
    "track_multi_enc",
    "track_stereo_enc",
    "track_channelbus",
    "track_pfl",
    "enc_main_azimuth",
    "enc_main_elevation",
    "enc_phones_solo",
)


class LayoutError(Exception):
    """The layout cannot be trusted, so nothing built on it may start.

    Every failure here is one that would otherwise be silent: an address sent
    with a placeholder still in it, a track number defaulted to zero. REAPER
    ignores what it does not understand, so the loudest such failure is no
    failure at all -- which is why these are raised rather than logged.
    """


class Channel:
    """One channel's REAPER tracks, by the names the code already used."""

    def __init__(self, values):
        for field in CHANNEL_FIELDS:
            if field not in values:
                raise LayoutError(f"channel is missing {field}")
            setattr(self, field, int(values[field]))


class Layout:
    def __init__(self, channels, addresses):
        self._channels = channels
        self._addresses = addresses

    def channel(self, index):
        if index < 0 or index >= len(self._channels):
            raise LayoutError(f"no channel {index}; the layout has "
                              f"{len(self._channels)}")
        return self._channels[index]

    @property
    def channel_count(self):
        return len(self._channels)

    def address(self, name, **values):
        """The address `name`, with its placeholders filled in.

        Missing one is an error, not a message with a brace in it: REAPER
        drops what it cannot parse without a word, so a typo would show up as
        a control that does nothing rather than as a fault.
        """
        if name not in self._addresses:
            raise LayoutError(f"no address named {name}")

        try:
            return self._addresses[name].format(**values)
        except KeyError as missing:
            raise LayoutError(
                f"address {name} still wants {missing}") from missing


def load_layout(path):
    """Read a layout file, or refuse.

    Nothing is defaulted. A layout that cannot be read is a Core that would
    drive the wrong tracks, and coming up wrong is worse than not coming up.
    """
    path = Path(path)
    try:
        text = path.read_text()
    except OSError as problem:
        raise LayoutError(f"cannot read {path}: {problem}") from problem

    try:
        parsed = json.loads(text)
    except json.JSONDecodeError as problem:
        raise LayoutError(f"{path} is not JSON: {problem}") from problem

    channels = [Channel(entry) for entry in parsed.get("channels", [])]
    addresses = dict(parsed.get("addresses", {}))

    return Layout(channels, addresses)
