"""A send that fails is said once, and does not end the message.

Right after a boot the network can come up after Core; a send then raises
"Network is unreachable". Raised inside a handler it ended the handler, so
the destinations after the failing one and the rest of the message got
nothing. Here the error is caught and said once per destination until a
send to it gets through again -- a dead destination must neither stop the
others nor fill the journal. Only OSError is caught: anything else is a
fault in Core, not the network, and must stay loud.
"""


class SendFailures:
    def __init__(self, say=print):
        self._say = say
        self._failing = set()

    def send(self, destination, send):
        """Run `send()`; True if it got through."""
        try:
            send()
        except OSError as problem:
            if destination not in self._failing:
                self._failing.add(destination)
                self._say(f"{destination}: send failed ({problem.strerror or problem}); "
                          f"said once until it gets through again")
            return False
        if destination in self._failing:
            self._failing.discard(destination)
            self._say(f"{destination}: sends get through again")
        return True
