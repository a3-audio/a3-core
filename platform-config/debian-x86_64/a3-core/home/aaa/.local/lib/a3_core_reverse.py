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

**What is deliberately not reversed:**

- The stereo/multi crossfade (3d, fx-send). One input becomes two gains on two
  tracks; a single number cannot say which input it came from, and
  a3_core_curves refuses it rather than inventing one.
- The toggles (pfl, fx, 4d). They are Core's own state, not REAPER's, and
  they come back from REAPER as a mute rather than as the flag they set.

Both are counted and logged by the receiver instead, so what is not handled is
visible rather than merely absent.
"""

from collections import namedtuple

#: One way back. `field` is which of a channel's tracks it is, `slot` and
#: `param` locate the plugin parameter, `curve` is what bent it on the way
#: out, and `address` is the A3 message it came from.
Reverse = namedtuple("Reverse", "field slot param curve address")

#: Keyed the way a REAPER address arrives, once the track has been resolved to
#: a channel: (field, fx slot, fx parameter).
#:
#: The slot is written as the layout's name rather than a number so this reads
#: as "the gain plugin" and not "plugin 1", and so it follows a project where
#: a plugin moves.
CHANNEL_REVERSALS = (
    Reverse("track_input", "gain", 1, "slope_volume", "gain"),
    Reverse("track_input", "eq", 1, "slope_eq", "eq/high"),
    Reverse("track_input", "eq", 2, "slope_eq", "eq/mid"),
    Reverse("track_input", "eq", 3, "slope_eq", "eq/low"),
    Reverse("track_input", "hipass", 7, "slope_fx_freq_hipass", "frequency"),
    Reverse("track_input", "hipass", 6, "slope_fx_res", "resonance"),
    Reverse("track_channelbus", None, None, "slope_volume", "volume"),
)


def reverse_for(layout, address, channel_field):
    """Which A3 message a REAPER address stands for, or None.

    `channel_field` is what layout.track_role() said the track is. The address
    still has to be taken apart for the slot and the parameter, because one
    track carries several plugins and one plugin several values.
    """
    parts = address.strip("/").split("/")

    # /track/T/fx/S/fxparam/P/value
    if len(parts) == 7 and parts[2] == "fx" and parts[4] == "fxparam":
        try:
            slot, param = int(parts[3]), int(parts[5])
        except ValueError:
            return None

        for entry in CHANNEL_REVERSALS:
            if entry.field != channel_field or entry.slot is None:
                continue
            if layout.fx_slot(entry.slot) == slot and entry.param == param:
                return entry

        # The channelbus gain carries one value on several parameters; any of
        # them says the same thing, so the first is taken and the rest are
        # that value again.
        if channel_field == "track_channelbus" and slot == 1:
            if param in layout.gain_params("channelbus"):
                return CHANNEL_REVERSALS[-1]

    return None
