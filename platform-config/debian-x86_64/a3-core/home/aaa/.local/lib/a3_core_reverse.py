"""Turning REAPER's feedback back into an A3 message.

Core's handlers send an A3 value out to a REAPER track, slot and parameter,
bending it through a curve. This is that read backwards: a REAPER address and
value in, the A3 address and value out.

**It is a second table, and that is the thing to watch.** The forward path is
an if/elif chain in a3-core.py and this has to agree with it. Two tables that
must agree drift, and the drift is silent: a knob would report the wrong
value, or none. So tools/tests/test_reverse_covers_forward.py holds this
against the source -- it walks the handlers, finds every (slot, param, curve)
they send, and insists there is an entry here for it.

The right shape is one table read both ways, with the handlers driven from it.
That is a real refactor of the live message path and is not this change.

**Who is told is no longer written here.** Every entry used to carry a `to`
naming one device, and that field is gone: as of 2026-09-12 every A3-shaped
message goes to every subscriber. The reason is the bug that field caused.
It said "mixer" for as long as only the desk had a channel strip, A3 Motion
grew one, the sentence quietly stopped being true, and nothing failed --
because a message nobody is told to send is an absence, not an error. A list
of recipients per message is a list that goes stale. See
a3-core.py's `broadcast`.

**What is deliberately not reversed:**

- The stereo/multi crossfade (`3d`). One input becomes two gains on two
  tracks; a single number cannot say which input it came from, and
  a3_core_curves refuses it rather than inventing one. It is answered for
  anyway, from the other end: Core holds what it was sent and replays that,
  the way it replays the position -- see a3_core_recall.REMEMBERED_CONTROLS.

- The position (azimuth, elevation). It never goes to a REAPER track at all --
  Core writes it straight to the IEM plugins' own OSC port -- so there is
  nothing for REAPER to report. Core remembers it instead; same place, same
  reason.

- The toggles (pfl, fx, 4d) and the filter mode. They are Core's own state,
  not REAPER's, and they come back from REAPER as a mute rather than as the
  flag they set. Core broadcasts them itself, on the addresses they arrive on.

- The encoder pots (pot_1, pot_2, and 3d again). **These must never be
  relayed continuously**, and the rule is exact: an action script can drive
  them, so REAPER holds the base value with the accent envelope on top while
  the device holds only the base. Writing the one into the other makes every
  accent's peak the new base. Built 2026-09-12, live for a few hours, taken
  out the same day -- see
  issues/a3-core-dauernder-rueckweg-ist-eine-schleife.md.

Everything else is here, including the master section and the shared filter,
which had no way back at all until 2026-09-12.
"""

from collections import namedtuple

#: How the A3 address is built.
#:
#: `CHANNEL` entries carry a suffix and are hung under the channel whose track
#: reported them -- "gain" becomes /channel/2/gain. `GLOBAL` entries carry the
#: whole address, because the control belongs to no channel.
#:
#: The filter is the reason this is a property of the entry rather than of the
#: table it sits in: /fx/frequency is one control, and it is written to all
#: four channels' input tracks. The report therefore arrives on a *channel's*
#: track and the answer is still global.
CHANNEL = "channel"
GLOBAL = "global"

#: What kind of REAPER address the value comes back on. Three shapes, because
#: REAPER has three ways of reporting the things A3 sets.
FXPARAM = "fxparam"   # /track/T/fx/S/fxparam/P/value
SEND = "send"         # /track/T/send/S/volume
VOLUME = "volume"     # /track/T/volume

#: `param` for a plugin that holds one value on several parameters at once.
#: Any of them says the same thing, so the first to arrive is taken and the
#: rest are that value again.
GAINS = "gains"

#: Which of the layout's gain-parameter lists belongs to which track field.
#: Read when `param` is GAINS.
GAIN_PARAMS_OF = {
    "track_channelbus": "channelbus",
    "track_masterbus": "masterbus",
    "track_booth": "boothbus",
    "aux_return": "aux_return",
}

#: One way back.
#:
#: `kind` is which of the three REAPER address shapes it arrives on, `field`
#: is which track it is (a channel's or the master's, by the names
#: a3_core_layout uses), `slot` and `param` locate the value on that track,
#: `curve` is what bent it on the way out, `address` is the A3 message it came
#: from, and `scope` says whether that address is a channel's or the rig's.
#:
#: `slot` means an fx slot for FXPARAM, a send name for SEND, and nothing for
#: VOLUME. `param` means a parameter number, or GAINS, or nothing.
Reverse = namedtuple("Reverse", "kind field slot param curve address scope")

#: Every value REAPER reports that A3 has a name for.
#:
#: **Per channel** first, then the two that are one control written to all
#: four channels, then the master's. The order is only for reading; lookup is
#: by (field, kind, slot, param) and no two entries may share those -- a test
#: insists on it, because a duplicate would make which of them answers depend
#: on the order of this tuple.
REVERSALS = (
    # The channel strip.
    Reverse(FXPARAM, "track_input", "gain", 1,
            "slope_volume", "gain", CHANNEL),
    Reverse(FXPARAM, "track_input", "eq", 1,
            "slope_eq", "eq/high", CHANNEL),
    Reverse(FXPARAM, "track_input", "eq", 2,
            "slope_eq", "eq/mid", CHANNEL),
    Reverse(FXPARAM, "track_input", "eq", 3,
            "slope_eq", "eq/low", CHANNEL),
    Reverse(FXPARAM, "track_channelbus", "gain", GAINS,
            "slope_volume", "volume", CHANNEL),

    # The FX send, which is a send and not a parameter -- it leaves the track
    # rather than sitting on it. No action script drives it, so relaying it is
    # as safe as relaying the gain.
    Reverse(SEND, "track_channelbus", "fx", None,
            "slope_constant_power", "fx-send", CHANNEL),

    # The one filter all four channels share. Written to every channel's input
    # track and read back from whichever reports first; the answer is the same
    # number either way, and `broadcast` drops the repeats.
    #
    # The hipass is read and the lopass is not. Both carry the same A3 value
    # through different curves, so either would do, and reading both would be
    # answering one control twice.
    Reverse(FXPARAM, "track_input", "hipass", "filter_frequency",
            "slope_fx_freq_hipass", "/fx/frequency", GLOBAL),
    Reverse(FXPARAM, "track_input", "hipass", "filter_resonance",
            "slope_fx_res", "/fx/resonance", GLOBAL),

    # The master section. None of it belongs to a channel, which is why the
    # first version of this table had nowhere to put it -- and why the master
    # page of A3 Motion's mixer came up at its own defaults for as long as it
    # existed.
    Reverse(FXPARAM, "track_masterbus", "gain", GAINS,
            "slope_volume", "/master/volume", GLOBAL),
    Reverse(FXPARAM, "track_booth", "gain", GAINS,
            "slope_volume", "/master/booth", GLOBAL),
    Reverse(FXPARAM, "track_phones", "phones_gain", 1,
            "slope_volume", "/master/phones_volume", GLOBAL),
    Reverse(FXPARAM, "aux_return", "aux_gain", GAINS,
            "slope_constant_power", "/master/return", GLOBAL),

    # The headphone mix is the one value that goes out unbent, as a plain
    # track volume. `identity` is a3_core_curves' name for that, so this reads
    # as a control with no curve rather than as a hole in the table.
    Reverse(VOLUME, "track_ph_mix", None, None,
            "identity", "/master/phones_mix", GLOBAL),
)

#: Kept under its old name so the coverage test and older readers still find
#: it. The split into channel and master entries is not a distinction this
#: table makes any more -- the field says which track it is.
CHANNEL_REVERSALS = REVERSALS


def _slot_number(layout, entry):
    """The REAPER slot an entry's `slot` name stands for."""
    return layout.fx_slot(entry.slot)


def _param_matches(layout, entry, param):
    """Whether a reported parameter number is the one this entry means.

    Three spellings, and each is a fact about the plug-in rather than a
    convenience: a number is one parameter, GAINS is a gain plug-in holding
    one value across several, and a string is a name in the layout's
    fx_params -- so `filter_frequency` reads as what it carries instead of
    as 7.
    """
    if entry.param == GAINS:
        return param in layout.gain_params(GAIN_PARAMS_OF[entry.field])
    if isinstance(entry.param, str):
        return param == layout.fx_param(entry.param)
    return param == entry.param


def _parse(address):
    """The REAPER address, as (kind, slot, param), or None.

    Three shapes and nothing else. Everything REAPER says that is not one of
    them -- and that is most of what it says -- is somebody else's business
    and is counted rather than guessed at.
    """
    parts = address.strip("/").split("/")
    if not parts or parts[0] != "track":
        return None

    try:
        if len(parts) == 7 and parts[2] == "fx" and parts[4] == "fxparam":
            return FXPARAM, int(parts[3]), int(parts[5])
        if len(parts) == 5 and parts[2] == "send" and parts[4] == "volume":
            return SEND, int(parts[3]), None
        if len(parts) == 3 and parts[2] == "volume":
            return VOLUME, None, None
    except ValueError:
        return None

    return None


def reverse_for(layout, address, role_field):
    """Which A3 message a REAPER address stands for, or None.

    `role_field` is what layout.track_role() or layout.master_role() said the
    track is. The address still has to be taken apart, because one track
    carries several plugins and one plugin several values.
    """
    parsed = _parse(address)
    if parsed is None:
        return None

    kind, slot, param = parsed

    for entry in REVERSALS:
        if entry.kind != kind or entry.field != role_field:
            continue

        if kind == VOLUME:
            return entry

        if kind == SEND:
            if layout.send(entry.slot) == slot:
                return entry
            continue

        if (_slot_number(layout, entry) == slot
                and _param_matches(layout, entry, param)):
            return entry

    return None


def reversed_address(layout, entry, channel_index):
    """The A3 address this value comes back on.

    A channel's controls are hung under the channel whose track reported them;
    everything else carries its whole address already. `channel_index` is
    ignored for the second kind and may be None -- a master track has no
    channel, and demanding one would mean inventing a number to throw away.
    """
    if entry.scope == CHANNEL:
        return layout.address("channel_control", channel=channel_index,
                              control=entry.address)
    return entry.address
