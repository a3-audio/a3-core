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
  report it. Motion has a fader for it since the same day, so the reason this
  entry used to give -- "the only device that sets it is an analog pot" -- has
  run out. It is left alone for now because 0 is the right value to come up
  on and a recall already delivers that, not because nothing could be done.
  See issues/a3-motion-ui-der-mixer-uebernimmt-cores-werte-nicht.md.

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
#: out, `address` is the A3 message it came from, and `to` is the tuple of
#: devices that are told about it.
#:
#: `to` is a tuple and not a name. It held a single string until 2026-09-12,
#: which is exactly as long as only one device had these controls -- and a
#: string left in there would iterate into its own letters and send to six
#: devices that do not exist, quietly, because OSC over UDP has no way of
#: saying otherwise.
Reverse = namedtuple("Reverse", "field slot param curve address to")

#: Keyed the way a REAPER address arrives, once the track has been resolved to
#: a channel: (field, fx slot, fx parameter).
#:
#: The slot is the layout's name rather than a number, so this reads as "the
#: gain plugin" and follows a project where a plugin moves.
#:
#: `to` is which devices the message goes back to, and since 2026-09-12 every
#: entry says **both**.
#:
#: It said "mixer" alone for as long as only the desk had a channel strip, and
#: the note here said so: *"telling Motion about them would be telling it
#: about controls it does not have."* A3 Motion grew a software strip on
#: 2026-09-10 and that sentence stopped being true without anything failing --
#: the maintainer found GAIN and VOL sitting at zero on a rig that was making
#: sound, three days later.
#:
#: **Why both, and why continuously.** Both, because the desk is still the
#: desk; taking its feed away would be a second decision, and nobody asked for
#: one. Continuously rather than only on a recall, because none of these five
#: carries an accent envelope -- REAPER's value *is* the device's value, so
#: there is nothing for a relay to ratchet up. That is precisely what
#: separates them from the two encoder pots below, which were relayed for a
#: few hours the same day and had to come out.
#:
#: **What is not fixed by this.** `a3-mixer.py` subscribes to `/vu/*`,
#: `/channel/*/led/*` and `/fx/led` -- and to nothing else. These five have
#: been arriving at a device that drops them without a word for as long as
#: the table has existed. Sending them is still right: the desk is where
#: they belong and its displays are the obvious ear. But nobody should read
#: this table and conclude the desk is being kept up to date.
#:
#: **Only per-channel controls are here.** The filter's frequency and
#: resonance arrive on /fx/*, which is global rather than per channel -- the
#: first version of this table gave them a channel address, which would have
#: sent four contradictory messages for one control. They need their own way
#: back and do not have one yet.
#: Who is told about a per-channel control. Written once rather than five
#: times: the next control added would otherwise be the one that quietly gets
#: a shorter list.
BOTH_MIXERS = ("mixer", "motion")

CHANNEL_REVERSALS = (
    Reverse("track_input", "gain", 1, "slope_volume", "gain", BOTH_MIXERS),
    Reverse("track_input", "eq", 1, "slope_eq", "eq/high", BOTH_MIXERS),
    Reverse("track_input", "eq", 2, "slope_eq", "eq/mid", BOTH_MIXERS),
    Reverse("track_input", "eq", 3, "slope_eq", "eq/low", BOTH_MIXERS),
    Reverse("track_channelbus", None, None, "slope_volume", "volume",
            BOTH_MIXERS),

    # Motion's two encoder pots were here for a few hours on 2026-09-12 and
    # had to come out: relaying them continuously is a feedback loop. REAPER
    # holds what is *sounding* -- the base value with the accent envelope on
    # top -- while Motion holds the base. Writing the one into the other made
    # every accent's peak the new base, so an action raised the value and it
    # never came down. See
    # issues/a3-core-dauernder-rueckweg-ist-eine-schleife.md.
)



def reversed_messages(layout, entry, channel_index, a3_value):
    """Where one value REAPER reported goes: one message per device.

    Yields `(device, address, value)` -- deliberately the same shape
    a3_core_recall.recall_messages yields, because the caller does the same
    two things with both (note it as relayed, then send it) and one shape
    means one loop rather than two that drift.

    The address is built once and shared. Both devices set the control on the
    same address, so both are told on it; a per-device spelling would be a
    second protocol to keep in step, and its drift would show as one device
    going quiet for one control.
    """
    out = layout.address("channel_control", channel=channel_index,
                         control=entry.address)
    for device in entry.to:
        yield device, out, a3_value


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
