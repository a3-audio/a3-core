"""Core, das seinen Stand noch einmal sagt.

A device that has just started knows nothing about the room. There is no new
message format for that: Core **replays**. It sends every value exactly as it
would have been sent when it changed, so the receivers that already exist take
it without having to understand anything new, and the recall is the same set
of messages as an evening's worth of turning knobs -- only faster, and
testable without inventing a second vocabulary.

The state comes from three different places and it matters which:

- **The flags** are Core's own. The three a channel carries and the filter
  mode exist nowhere else, and a3_core_state has just read them off disk.
- **The position and the crossfade** are Core's own too, but for a different
  reason -- in fact for two. The position reaches the IEM plugins on their
  own OSC port rather than through a REAPER track, so REAPER never reports it
  back: the plugins hold it, but they are written to, not read from. The
  crossfade does reach REAPER, as two gains on two tracks, and a single
  number cannot say which input produced them. Nobody was told, or nobody can
  be asked; either way Core passed it on and Core is the only one who knows.
  See REMEMBERED_CONTROLS.
- **The continuous values** are REAPER's. Core does not hold them -- it
  relays them -- so what it replays is what REAPER last said rather than a
  second opinion that could disagree with it.

**A cold Core answers short.** REAPER reports on change and does not know Core
went away, so straight after Core's own restart nothing has been relayed and
the answer is the flags alone. That is on purpose: an answer a caller can act
on beats silence, which is indistinguishable from a Core that is not running.
The **position** behaves the same way and for the same reason: it is held in
memory only, so Core's own restart loses it, and an unknown position is left
unsaid rather than guessed at. The **crossfade** does survive a restart --
a3_core_state keeps it, because a knob changes when a hand turns it rather
than continuously the way a trajectory moves a position.
"""

#: Which flag lights which lamp, and what the lamp is told.
#:
#: One table, four readers: the three branches in osc_handler_channel that
#: flip a flag, and the recall. Written out in each of them, the fourth would
#: eventually disagree with the other three and the disagreement would only
#: show as a light that is wrong after a restart.
#:
#: **pfl is sent the other way round and that is correct.** Core sends "not
#: pfl" and a3-mixer.py inverts it again in send_button_leds_data (`0 if
#: led_on else 255`, for led_mode 0, which is pfl's). The two cancel: pfl on
#: is a lit button. It looks exactly like a bug, which is why it says so here.
LED_OF = {
    "pfl": ("led_pfl", lambda channel: float(not channel.toggle_pfl)),
    "fx": ("led_fx", lambda channel: float(channel.toggle_fx)),
    "3d": ("led_3d", lambda channel: float(channel.toggle_3d)),
}

#: How each filter mode is spelled on the wire. Written out rather than taken
#: from the enum's name or value: both would make a rename of a member a
#: silent change to the protocol, and the mixer is at the other end of it.
FX_MODE_WORDS = {
    "LOW_PASS": "low_pass",
    "HIGH_PASS": "high_pass",
}


def led_message(layout, flag, index, channel):
    """The one message that says what a channel's `flag` lamp should do."""
    name, read = LED_OF[flag]
    return layout.address(name, channel=index), read(channel)


#: What Core holds because nobody else can be asked, as (address, field).
#:
#: Address and field are the same word except where they cannot be: `3d` is
#: not a name Python will take. Keeping them together in one table means a
#: rename cannot make the replay disagree with the message it replays.
#:
#: The two reasons are different and both end here. The **position** never
#: reaches a REAPER track at all -- Core writes it straight to the IEM
#: plugins' own OSC port -- so there is nothing for REAPER to report. The
#: **crossfade** does reach REAPER, as two gains on two tracks, and a single
#: number cannot say which input produced them; a3_core_curves refuses to
#: guess. Nobody was told, or nobody can be asked: either way Core is the
#: only one who knows.
REMEMBERED_CONTROLS = (
    ("azimuth", "azimuth"),
    ("elevation", "elevation"),
    ("3d", "three_d"),
)


def remembered_messages(layout, channels):
    """What Core holds for each channel, as the messages that set it.

    A value Core has never seen is left out rather than sent as 0.0. Zero
    degrees is the front of the room -- a real position -- so answering it
    would place the sound somewhere while claiming to report where it
    already is. Left out, Motion keeps its own value, which is what happens
    today anyway. Hence `is None` and not a falsiness test: front-centre and
    level is where a channel most often sits, and 0.0 is a real crossfade
    setting too.
    """
    for index, channel in enumerate(channels):
        for address, field in REMEMBERED_CONTROLS:
            value = getattr(channel, field, None)
            if value is None:
                continue
            yield ("motion",
                   layout.address("channel_control", channel=index,
                                  control=address),
                   value)


def flag_messages(layout, channels, master):
    """Every lamp Core is responsible for, channel by channel, then the
    filter mode."""
    for index, channel in enumerate(channels):
        for flag in LED_OF:
            address, value = led_message(layout, flag, index, channel)
            yield "mixer", address, value

    yield ("mixer", layout.address("fx_mode_led"),
           FX_MODE_WORDS[master.fx_mode.name])


class Relayed:
    """The last value Core passed on, per device and address.

    In memory and nowhere else. A file would be a copy of REAPER's state read
    back at a moment when REAPER may have moved on; this cannot be older than
    the last message REAPER sent, because it *is* that message.
    """

    def __init__(self):
        self._values = {}

    def note(self, device, address, value):
        self._values[(device, address)] = value

    def messages(self):
        """In the order the controls were first touched, so the same evening
        replays the same way twice."""
        for (device, address), value in self._values.items():
            yield device, address, value

    def __len__(self):
        return len(self._values)


def recall_messages(layout, channels, master, relayed):
    """The whole answer: the lamps, then the positions, then what REAPER said.

    Core's own two certainties first -- the lamps come from its state file and
    are complete even on a cold start, the remembered values from what it last
    passed on -- and REAPER's relayed values last. A caller reading the replay in
    order sees what Core knows for itself before what it was told.
    """
    yield from flag_messages(layout, channels, master)
    yield from remembered_messages(layout, channels)
    yield from relayed.messages()
