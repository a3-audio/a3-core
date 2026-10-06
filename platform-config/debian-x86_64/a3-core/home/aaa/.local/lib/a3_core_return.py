"""Where the two return channels go: the StemDeck that is played, or radla.

The return is REAPER's rec bus, sent by zita-j2n. One StemDeck at a time, and
Core follows the one that said hello last; the return follows it too
(maintainer, 2026-10-06). A StemDeck on another machine gets it; one on the
rig itself, or none, means radla, as before. A StemDeck machine listens on the
truth's radla.zita-n2j port (StemDeck's zita-from-truth.py n2j), so only the
host changes.

The target reaches zita-j2n through a small env file its unit reads after
osc.env, and a restart of the unit: a short dropout on the return, accepted.
"""

import subprocess
import threading
from pathlib import Path

from a3_core_motion import is_this_machine
from a3_osc_render import lines

#: Read by zita-j2n.service after osc.env, so its two values win.
RETURN_ENV_FILE = Path(".config/a3/zita-return.env")
HOST_KEY = "A3_ZITA_J2N_HOST"
PORT_KEY = "A3_ZITA_J2N_PORT"

RESTART = ("systemctl", "--user", "restart", "zita-j2n.service")


def return_target(stemdeck_host, own_hosts, radla):
    """The (host, port) zita-j2n sends to."""
    if stemdeck_host is None or is_this_machine(stemdeck_host, own_hosts):
        return tuple(radla)
    return stemdeck_host, radla[1]


class ReturnTarget:
    def __init__(self, radla, own_hosts, current=None):
        self.radla = tuple(radla)
        self._own_hosts = frozenset(own_hosts)
        self.current = tuple(current) if current is not None else self.radla

    def follow(self, stemdeck_host):
        """The new target when the active StemDeck moves the return, else None."""
        wanted = return_target(stemdeck_host, self._own_hosts, self.radla)
        if wanted == self.current:
            return None
        self.current = wanted
        return wanted


def return_env(target):
    host, port = target
    return lines([(HOST_KEY, host), (PORT_KEY, port)])


def read_return_env(home):
    """The target the file names, or None when there is none to be read."""
    try:
        text = (home / RETURN_ENV_FILE).read_text()
    except OSError:
        return None
    values = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
    try:
        return values[HOST_KEY], int(values[PORT_KEY])
    except (KeyError, ValueError):
        return None


def write_return_env(home, target):
    path = home / RETURN_ENV_FILE
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(return_env(target))


def restart_zita(run=subprocess.run):
    """Restart zita-j2n on a thread of its own: systemctl waits for zita to
    stop and start again, and Core's serve loop must not wait with it."""
    thread = threading.Thread(target=run, args=(RESTART,),
                              kwargs={"check": False}, daemon=True,
                              name="a3-zita-restart")
    thread.start()
    return thread


def point_zita_at(target, home, restart=restart_zita):
    """Write the target, then restart: the unit reads the file at its start."""
    write_return_env(home, target)
    restart()


def start_at_radla(radla, own_hosts, home, restart=restart_zita):
    """The return as Core comes up, knowing no StemDeck: radla. The file is
    always written; zita is restarted only if it named somewhere else -- a
    remote StemDeck from before a reboot is not there until it says hello."""
    target = ReturnTarget(radla, own_hosts, current=read_return_env(home))
    moved = target.follow(None)
    write_return_env(home, target.current)
    if moved is not None:
        restart()
    return target
