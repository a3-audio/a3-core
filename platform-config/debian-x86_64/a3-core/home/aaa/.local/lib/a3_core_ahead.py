"""StemDeck's preview of the music, on its way to Motion.

StemDeck says where the audible deck is in its set's sections --
/stemdeck/ahead section next bars energy -- on every downbeat, to Core,
because Core is the one that knows which Motion is listening (the rig's own
or a remote one, a3_core_motion). Core passes it on unchanged, and to Motion
only: the desk has no use for it.

It is never noted and never replayed. A preview is about the next bars; one
replayed to a Motion that has just arrived would tell it about bars long
gone. Motion counts a preview older than four bars as absent anyway.
"""

#: section, next, bars until next, energy -- as the truth's "ssif" says.
ARGUMENT_TYPES = (str, str, int, float)


def ahead_arguments(arguments):
    """The arguments as they go on to Motion, or None if they are not a
    preview. An energy that arrived as a whole number goes on as a float, so
    the types on the wire stay "ssif"."""
    if len(arguments) != len(ARGUMENT_TYPES):
        return None
    section, following, bars, energy = arguments
    if not isinstance(section, str) or not isinstance(following, str):
        return None
    if isinstance(bars, bool) or not isinstance(bars, int):
        return None
    if isinstance(energy, bool) or not isinstance(energy, (int, float)):
        return None
    return [section, following, bars, float(energy)]
