"""Core, das seinen Stand noch einmal sagt.

A device that has just started knows nothing about the room. There is no new
message format for that: Core **replays**. It sends every value exactly as it
would have been sent when it changed, so the receivers that already exist take
it without having to understand anything new, and the recall is the same set
of messages as an evening's worth of turning knobs -- only faster, and
testable without inventing a second vocabulary.

The state comes from two different places and it matters which:

- **The flags** are Core's own. The three a channel carries and the filter
  mode exist nowhere else, and a3_core_state has just read them off disk.
- **The continuous values** are REAPER's. Core does not hold them -- it
  relays them -- so what it replays is what REAPER last said rather than a
  second opinion that could disagree with it.

**A cold Core answers short.** REAPER reports on change and does not know Core
went away, so straight after Core's own restart nothing has been relayed and
the answer is the flags alone. That is on purpose: an answer a caller can act
on beats silence, which is indistinguishable from a Core that is not running.
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
    """The whole answer: the lamps first, then what REAPER last reported.

    The lamps first because they are the part that is certain -- they come
    from Core's own file and are complete even on a cold start.
    """
    yield from flag_messages(layout, channels, master)
    yield from relayed.messages()
