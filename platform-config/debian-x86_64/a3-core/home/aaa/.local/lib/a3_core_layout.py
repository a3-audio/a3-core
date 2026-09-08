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

#: The fields of CHANNEL_FIELDS that are REAPER track numbers.
#:
#: The others -- enc_main_azimuth and friends -- are a different numbering
#: altogether, and answering one of them as a track would put a value on the
#: wrong thing: enc_main_azimuth is 8 on channel 0, and track 8 is the
#: master's ph_mix.
TRACK_FIELDS = (
    "track_input",
    "track_multi_enc",
    "track_stereo_enc",
    "track_channelbus",
    "track_pfl",
)

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
    def __init__(self, channels, master, fx_slots, fx_params, gain_params,
                 addresses):
        self._channels = channels
        self._master = master
        self._fx_slots = fx_slots
        self._fx_params = fx_params
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

    def track_role(self, track):
        """Which channel a REAPER track belongs to, and as what.

        The map has always been read the other way -- channel to track,
        because that is the direction messages travel. REAPER's feedback comes
        back naming a track, so the same file has to answer both, and it is
        derived rather than written down twice: a second list is a second
        thing to keep in step, and that goes wrong silently -- a value landing
        on the wrong channel's knob.

        Returns (channel index, field name), or None for a track no channel
        claims: the master's, and anything REAPER has that A3 does not name.
        """
        for index, channel in enumerate(self._channels):
            for field in TRACK_FIELDS:
                if getattr(channel, field) == track:
                    return (index, field)
        return None

    def channel_for_track(self, track):
        """Just the channel, for callers that do not care which track it is."""
        found = self.track_role(track)
        return None if found is None else found[0]

    def fx_param(self, name):
        """Which parameter of a plugin carries a given value.

        `fxparam/8` said where the elevation sits and nothing about what it
        is; finding out meant opening the REAPER project. Named here, the
        number is in one place and the call site reads as what it does.
        """
        if name not in self._fx_params:
            raise LayoutError(f"no fx parameter named {name}")
        return int(self._fx_params[name])

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

    def address(self, name, /, **values):
        """The address `name`, with its placeholders filled in.

        Missing one is an error, not a message with a brace in it: REAPER
        drops what it cannot parse without a word, so a typo would show up as
        a control that does nothing rather than as a fault.

        `name` is positional-only. It was found to have to be: an address
        with a `{name}` placeholder could never have it filled in, because the
        keyword collided with this parameter. That address has since been
        split into three of its own and the collision is gone -- the guard
        stays because the next such placeholder would hit it again, and
        because nothing is paid for it.
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
                  dict(parsed.get("fx_params", {})),
                  dict(parsed.get("gain_params", {})),
                  dict(parsed.get("addresses", {})))
