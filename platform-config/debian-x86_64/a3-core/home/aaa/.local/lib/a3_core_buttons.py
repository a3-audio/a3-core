"""What a button message asks Core to do, when two senders mean two things.

`/channel/n/pfl`, `/channel/n/fx`, `/channel/n/4d` and `/fx/mode` each arrive
from two places, and the same value does not mean the same thing in both:

  * The **A³ Mixer** is a Pi relaying its serial line verbatim. Its buttons are
    momentary, so what arrives is an *edge*: `"1"` while the finger is down,
    `"0"` when it lifts. Which state that lands in is Core's to decide, because
    only Core knows which state it was in.

  * **A³ Motion** is a key drawn on a screen. It sends the *state* it wants --
    1 for on, 0 for off -- which is what the OSC reference says the address
    carries, and what makes it survive UDP: repeat a state and the two ends
    converge, repeat an edge and one lost datagram leaves them one flip apart
    forever.

Core tells them apart by the argument's type, and that is not an accident of
this module: the mixer sends **text** all the way through (`a3-mixer.py` puts
`words[4]` straight onto the wire, and Core's own tap handler has always
compared against the string `"1"`), while Motion sends **numbers**
(`MixerState::onSend` uses `addFloat32` for all fifteen of its addresses).

**The coupling, said out loud:** if a3-mixer.py ever converts its serial words
to numbers before sending, its presses become states here and PFL turns
momentary -- on while the finger is down. Nothing in this repo can catch that,
so it is written here, in test_core_buttons.py, and in the OSC reference.
"""

from typing import Any, Union

class _NoChange:
    """What a release asks for: nothing.

    It refuses to be truth-tested on purpose. "Asks for nothing" and "asks for
    off" are the two things this whole module exists to keep apart, and
    `if not wanted:` would quietly merge them again -- dropping every Motion
    "switch it off" along with every mixer release. Written this way, that
    mistake raises instead of shipping.
    """

    def __repr__(self) -> str:
        return "NO_CHANGE"

    def __bool__(self) -> bool:
        raise TypeError(
            "NO_CHANGE is not a state -- compare it with `is`, do not test it")


#: Compare with `is NO_CHANGE`, never with `if not`.
NO_CHANGE = _NoChange()

#: The two words `/fx/mode` carries, and the only two the LED reply knows.
FX_MODE_HIGH_PASS = "high_pass"
FX_MODE_LOW_PASS = "low_pass"

#: Where a state divides. `MixerState::channelToggle` is `value > 0.5f`,
#: strictly, so a value reading as off on the screen it came from reads as off
#: here too.
_ON_ABOVE = 0.5


def _is_a_number(value: Any) -> bool:
    """A state, not an edge. bool passes -- it is an int, and an OSC boolean
    is as much a state as a float is."""
    return isinstance(value, (int, float)) and not isinstance(value, str)


def wanted_toggle(value: Any, current: bool) -> Union[bool, str]:
    """The state a button message asks a toggle to be in.

    Returns the wanted state, or NO_CHANGE when the message asks for nothing
    (which is what a momentary button's release asks for).
    """
    if _is_a_number(value):
        return bool(value > _ON_ABOVE)

    if value == "1":
        return not current

    return NO_CHANGE


def wanted_fx_mode(value: Any, current: str) -> str:
    """The filter mode a `/fx/mode` message asks for.

    Returns `current` for anything it cannot read. The predecessor did the
    opposite -- `high_pass = value == "high_pass"` made every unreadable
    value, every float Motion sent among them, mean LOW_PASS -- so a message
    Core did not understand still moved the filter, always the same way.
    """
    if _is_a_number(value):
        return FX_MODE_HIGH_PASS if value > _ON_ABOVE else FX_MODE_LOW_PASS

    if value in (FX_MODE_HIGH_PASS, FX_MODE_LOW_PASS):
        return value

    return current
