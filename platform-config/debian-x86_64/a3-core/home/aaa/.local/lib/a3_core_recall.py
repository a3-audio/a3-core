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
            yield (layout.address("channel_control", channel=index,
                                  control=address),
                   value)


#: Each flag's address on the wire, and the field it is held in.
#:
#: The lamp and the state are two different messages about one fact, and both
#: are sent. The **lamp** (`/channel/n/led/pfl`) is an instruction in the
#: desk's own convention -- pfl is inverted twice between here and the LED, see
#: LED_OF -- and only the desk understands it. The **state**
#: (`/channel/n/pfl`) is the flag itself, on the address it arrived on, in the
#: spelling every device already sends. A screen, a light desk or anything
#: added later reads that one and needs to know nothing about lamps.
#:
#: `3d` is the odd name: the toggle moved to `4d` when `3d` became the
#: continuous crossfade, and the field kept its old name because renaming a
#: field in the state file would have dropped every saved flag.
STATE_OF = {
    "pfl": ("pfl", lambda channel: float(channel.toggle_pfl)),
    "fx": ("fx", lambda channel: float(channel.toggle_fx)),
    "4d": ("4d", lambda channel: float(channel.toggle_3d)),
}

#: How the filter mode reads as a number: 1 is high pass.
#:
#: Sent as well as the word, and for the same reason as the flags above: the
#: word goes to the desk on `/fx/led`, the number goes to everyone on
#: `/fx/mode` -- which is the address the mode arrives on, and the spelling
#: A3 Motion already sends. See a3_core_buttons.wanted_fx_mode, which accepts
#: both on the way in.
FX_MODE_NUMBERS = {
    "HIGH_PASS": 1.0,
    "LOW_PASS": 0.0,
}


def lamp_messages(layout, channels, master):
    """What the lamps should do. **For the desk and nobody else.**

    A lamp is not a fact, it is an instruction in the desk's own convention --
    and pfl's is inverted here and inverted again in the desk's firmware,
    which cancels and looks exactly like a bug (see LED_OF). The filter mode
    goes out as a word on `/fx/led` for the same reason: it is what that
    firmware reads.

    So these travel the desk's private wire, the way REAPER's `/track/*` and
    the plugins' `/MultiEncoder/*` travel theirs. Broadcasting them would put
    an inverted pfl in front of every department added from now on, and the
    first one to trust it would be wrong with nothing to show for it.

    This is not the per-message recipient list that was deleted on
    2026-09-12. That one asked *who should hear this value*. This asks what
    kind of message it is -- and a lamp instruction in somebody's firmware
    convention is not the A3 protocol.
    """
    for index, channel in enumerate(channels):
        for flag in LED_OF:
            yield led_message(layout, flag, index, channel)

    yield (layout.address("fx_mode_led"),
           FX_MODE_WORDS[master.fx_mode.name])


def flag_messages(layout, channels, master):
    """The same flags as facts, on the addresses they arrive on. For everyone.

    A plain 0 or 1 on `/channel/n/pfl`, and the filter mode as a number on
    `/fx/mode` -- the spelling A3 Motion already sends, and the one
    a3_core_buttons already accepts on the way in. Nothing here needs to be
    learned: a device that can *set* the flag can read it.
    """
    for index, channel in enumerate(channels):
        for control, read in STATE_OF.values():
            yield (layout.address("channel_control", channel=index,
                                  control=control),
                   read(channel))

    yield "/fx/mode", FX_MODE_NUMBERS[master.fx_mode.name]


class Relayed:
    """The last value Core passed on, per address.

    Keyed by address alone since 2026-09-12. It used to be keyed by (device,
    address), from when a value went to one device -- which meant the same
    number was held twice once it went to two, and replayed twice. Every
    A3-shaped message now goes to every subscriber, so who heard it is no
    longer a property of the value.

    In memory and nowhere else. A file would be a copy of REAPER's state read
    back at a moment when REAPER may have moved on; this cannot be older than
    the last message REAPER sent, because it *is* that message.
    """

    def __init__(self):
        self._values = {}

    def holds(self, address, value):
        """Whether this is already what was last passed on.

        Asked before sending, because REAPER reports one A3 control on several
        parameters -- a gain plug-in holds its value across eight of them, and
        the shared filter is written to all four channels' tracks. Without
        this, one knob would become eight identical messages to every
        subscriber.
        """
        return address in self._values and self._values[address] == value

    def note(self, address, value):
        self._values[address] = value

    def messages(self):
        """In the order the controls were first touched, so the same evening
        replays the same way twice."""
        return iter(self._values.items())

    def __len__(self):
        return len(self._values)


def recall_messages(layout, channels, master, relayed):
    """The whole answer: the flags, then the positions, then what REAPER said.

    As `(address, value)` pairs and nothing about who gets them -- everything
    A3-shaped goes to every subscriber. See a3_core_subscribers for why that
    stopped being a per-message decision.

    Core's own two certainties first -- the flags come from its state file and
    are complete even on a cold start, the remembered values from what it last
    passed on -- and REAPER's relayed values last. A caller reading the replay
    in order sees what Core knows for itself before what it was told.

    The lamps are **not** here. They are the desk's own wire and its own
    convention; see lamp_messages.
    """
    yield from flag_messages(layout, channels, master)
    yield from remembered_messages(layout, channels)
    yield from relayed.messages()
