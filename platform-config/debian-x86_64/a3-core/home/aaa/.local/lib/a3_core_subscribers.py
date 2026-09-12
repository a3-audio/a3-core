"""Who gets told, and how a new one is added without touching this file.

Core has two kinds of outgoing client and they are not interchangeable:

- **The engine.** REAPER, the IEM MultiEncoders, the DualDelay. Each speaks
  its own vendor's language -- `/track/...`, `/MultiEncoder/...`,
  `/DualDelay/...` -- and each is addressed deliberately, by the one handler
  that has something to say to it.

- **The subscribers.** Anything that speaks the A3 protocol: the mixer, A3
  Motion, and whatever else is plugged in. Every A3-shaped message goes to
  **all** of them, always.

That second rule is the point of this module, and it replaced a per-message
list of recipients on 2026-09-12. The list had said "mixer" since before A3
Motion had a channel strip; Motion grew one, the list stayed, and nothing
failed -- a message nobody is told to send is an absence, not an error, and
OSC over UDP has no way of reporting one. Three days later the strip was found
sitting at zero on a rig that was making sound.

So there is no longer a question of *who* gets a value. There is only the
question of whether a message is A3-shaped, and if it is, everybody hears it.
A light desk or a video department is then a command-line argument rather than
a patch: `--subscriber light=192.168.43.60:7771`.

No pythonosc and no sockets: this is parsing and a dict, importable and
testable anywhere. Same door as a3_core_layout.
"""


class SubscriberError(Exception):
    """The subscriber cannot be understood, so Core will not start.

    Louder than a warning on purpose. A misspelled subscriber is a department
    that silently hears nothing all evening, and the whole reason this module
    exists is that silence of exactly that kind went unnoticed for three days.
    """


#: The two that ship, in the order they are told. The order is not arbitrary:
#: it is the order a recall replays in, so the same evening replays the same
#: way twice.
SHIPPED = ("mixer", "motion")


def parse_subscriber(text):
    """`name=host:port` -> (name, host, port).

    The name is what the window shows and what the traffic log records, so it
    has to be something a person can read in a table at two in the morning --
    `light`, `video`, `foh` -- rather than an address.
    """
    if "=" not in text:
        raise SubscriberError(
            f"--subscriber wants name=host:port, got {text!r}")

    name, _, endpoint = text.partition("=")
    name = name.strip()
    if not name:
        raise SubscriberError(f"--subscriber has no name: {text!r}")

    if ":" not in endpoint:
        raise SubscriberError(
            f"--subscriber {name} wants host:port, got {endpoint!r}")

    host, _, port = endpoint.rpartition(":")
    host = host.strip()
    if not host:
        raise SubscriberError(
            f"--subscriber {name} has no host -- write 127.0.0.1:PORT")

    try:
        port = int(port)
    except ValueError as problem:
        raise SubscriberError(
            f"--subscriber {name} has no port number: {port!r}") from problem

    if not 1 <= port <= 65535:
        raise SubscriberError(
            f"--subscriber {name} port out of range: {port}")

    return name, host, port


def parse_subscribers(texts, reserved=()):
    """Every `--subscriber`, refusing a name that is already taken.

    `reserved` is the names Core uses for something else -- the two that ship
    and the engine's clients. A second `motion` would make the window's peer
    column ambiguous and would quietly double every message; a subscriber
    called `reaper` would be named after a client it is not.
    """
    taken = set(reserved)
    found = []

    for text in texts:
        name, host, port = parse_subscriber(text)
        if name in taken:
            raise SubscriberError(f"there is already a {name}")
        taken.add(name)
        found.append((name, host, port))

    return found


#: The three A3-shaped families. Everything else that arrives -- `/beat`,
#: `/state/recall`, REAPER's `/track/...` -- is a command or another vendor's
#: language, not a value a screen shows.
FAMILIES = ("channel", "master", "fx")

#: Flags. These are passed on by `announce_flag()` and the filter-mode branch
#: instead, and those tell **everybody including the sender** -- a lamp is
#: status, so the desk's own lamp has to follow the desk's own key. Passing
#: them on here as well would send each of them twice.
ANNOUNCED_INSTEAD = ("pfl", "fx", "mode")

#: The position. Not a REAPER parameter at all -- it goes straight to the IEM
#: encoders -- and it arrives tens of thousands of times per channel while a
#: clip plays. A screen that wants it asks for it at start-up.
ASKED_FOR_INSTEAD = ("azimuth", "elevation")


def relay_on_arrival(address):
    """Whether a value that just arrived should go on to the other screens.

    **Why on arrival and not on REAPER's report:** REAPER does not report a
    change back to the surface that caused it, and Core *is* that surface. A
    knob turned on the desk therefore reached REAPER and no screen at all --
    measured at the rig on 2026-09-12. REAPER's report covers the other case,
    a hand inside REAPER, and keeps its own exclusions.

    **Why this is safe where the report was not:** what arrives here is what a
    hand set -- the base value, before any accent envelope lies on top. The
    ratchet of that morning came from passing on REAPER's value, which is base
    *plus* modulation, and writing it back as a base. Nothing here passes
    through REAPER.
    """
    parts = address.strip("/").split("/")
    if len(parts) < 2 or parts[0] not in FAMILIES:
        return False

    if parts[0] == "channel":
        if len(parts) < 3:
            return False
        parameter = parts[2]
    else:
        parameter = parts[1]

    return (parameter not in ANNOUNCED_INSTEAD
            and parameter not in ASKED_FOR_INSTEAD)


def everyone_but(subscribers, origin):
    """Every subscriber except the one named `origin`.

    The sender is left out because it is holding the control: telling it what
    it just said is the loop this whole reverse path keeps almost becoming.
    An origin Core cannot name -- REAPER's report, a recall, an unknown host --
    leaves nobody out, because an unnamed sender is not a reason to keep a
    screen dark.

    Reads only `.name`, so a WatchedClient and a stand-in behave alike.
    """
    return [client for client in subscribers if client.name != origin]
