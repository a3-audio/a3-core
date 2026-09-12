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

- The stereo/multi crossfade (`3d`). One input becomes two gains on two
  tracks; a single number cannot say which input it came from, and
  a3_core_curves refuses it rather than inventing one. It is answered for
  anyway, from the other end: Core holds what it was sent and replays that,
  the way it replays the position -- see a3_core_recall.REMEMBERED_CONTROLS.

- The FX send (`fx-send`). A REAPER send since 2026-09-12, and REAPER does
  report it -- but nothing asks for it back, because the only device that
  sets it is the desk's analog pot.

- The position (azimuth, elevation). It never goes to a REAPER track at all --
  Core writes it straight to the IEM plugins' own OSC port -- so there is
  nothing for REAPER to report. Core remembers it instead; same place, same
  reason.
- The toggles (pfl, fx, 4d). They are Core's own state, not REAPER's, and
  they come back from REAPER as a mute rather than as the flag they set.

Both are counted and logged by the receiver instead, so what is not handled is
visible rather than merely absent.
"""

from collections import namedtuple

#: One way back. `field` is which of a channel's tracks it is, `slot` and
#: `param` locate the plugin parameter, `curve` is what bent it on the way
#: out, and `address` is the A3 message it came from.
Reverse = namedtuple("Reverse", "field slot param curve address to")

#: Keyed the way a REAPER address arrives, once the track has been resolved to
#: a channel: (field, fx slot, fx parameter).
#:
#: The slot is the layout's name rather than a number, so this reads as "the
#: gain plugin" and follows a project where a plugin moves.
#:
#: `to` is which device the message goes back to. Every entry says "mixer"
#: today -- these are the mixer's channel strip, and telling Motion about
#: them would be telling it about controls it does not have. The field is not
#: a constant, though: a control of Motion's would say so, and two of them
#: briefly did.
#:
#: **Only per-channel controls are here.** The filter's frequency and
#: resonance arrive on /fx/*, which is global rather than per channel -- the
#: first version of this table gave them a channel address, which would have
#: sent four contradictory messages for one control. They need their own way
#: back and do not have one yet.
CHANNEL_REVERSALS = (
    Reverse("track_input", "gain", 1, "slope_volume", "gain", "mixer"),
    Reverse("track_input", "eq", 1, "slope_eq", "eq/high", "mixer"),
    Reverse("track_input", "eq", 2, "slope_eq", "eq/mid", "mixer"),
    Reverse("track_input", "eq", 3, "slope_eq", "eq/low", "mixer"),
    Reverse("track_channelbus", None, None, "slope_volume", "volume",
            "mixer"),

    # Motion's two encoder pots were here for a few hours on 2026-09-12 and
    # had to come out: relaying them continuously is a feedback loop. REAPER
    # holds what is *sounding* -- the base value with the accent envelope on
    # top -- while Motion holds the base. Writing the one into the other made
    # every accent's peak the new base, so an action raised the value and it
    # never came down. See
    # issues/a3-core-dauernder-rueckweg-ist-eine-schleife.md.
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
