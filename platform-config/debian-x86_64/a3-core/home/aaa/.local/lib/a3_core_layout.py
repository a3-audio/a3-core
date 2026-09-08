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

    _fields = CHANNEL_FIELDS

    def __init__(self, values):
        for field in self._fields:
            if field not in values:
                raise LayoutError(
                    f"{type(self).__name__.lower()} is missing {field}")
            setattr(self, field, int(values[field]))


#: The tracks that belong to no single channel.
MASTER_FIELDS = (
    "track_masterbus",
    "track_booth",
    "track_phones",
    "track_ph_mix",
    "aux_return",
)


class Master(Channel):
    """The master side of the REAPER project, by the same rules."""

    _fields = MASTER_FIELDS


class Layout:
    def __init__(self, channels, master, fx_slots, gain_params, addresses):
        self._channels = channels
        self._master = master
        self._fx_slots = fx_slots
        self._gain_params = gain_params
        self._addresses = addresses

    @property
    def master(self):
        return self._master

    def fx_slot(self, name):
        """Which FX slot on a track holds a given plugin.

        Named rather than numbered at the call site: FX_INDEX_EQ = 2 said
        where the EQ sits and nothing about why, and a slot that moves in the
        project has to be found by reading every f-string that used it.
        """
        if name not in self._fx_slots:
            raise LayoutError(f"no fx slot named {name}")
        return int(self._fx_slots[name])

    def gain_params(self, name):
        """Which parameters of a gain plugin carry its value.

        A gain plugin holds one value on several parameters at once, and the
        list of which differs per bus. It was written out at four call sites
        as a literal list in a for-loop.
        """
        if name not in self._gain_params:
            raise LayoutError(f"no gain parameter list named {name}")
        return list(self._gain_params[name])

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

    if "master" not in parsed:
        raise LayoutError(f"{path} has no master block")
    master = Master(parsed["master"])

    return Layout(channels, master,
                  dict(parsed.get("fx_slots", {})),
                  dict(parsed.get("gain_params", {})),
                  dict(parsed.get("addresses", {})))
