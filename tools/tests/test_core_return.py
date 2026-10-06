"""Where Core's two return channels go: to the StemDeck that is played, or to
radla (decided 2026-10-06).

One StemDeck at a time, and Core follows the one that said hello last. The
return (REAPER's rec bus, sent by zita-j2n) follows it too: a StemDeck on
another machine gets it; a StemDeck on the rig itself, or none, means radla
as before. A switch restarts zita-j2n, a short dropout on the return.
"""

import sys
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))

from a3_core_return import (RESTART, RETURN_ENV_FILE, ReturnTarget,   # noqa: E402
                            point_zita_at, read_return_env, restart_zita,
                            return_env, return_target, start_at_radla)

RADLA = ("192.168.43.96", 55100)
OWN = {"192.168.8.10"}
A3NUC2 = "192.168.8.20"
NOTEBOOK = "192.168.43.70"


class TheDecision(unittest.TestCase):
    def test_no_stemdeck_is_radla(self):
        self.assertEqual(return_target(None, OWN, RADLA), RADLA)

    def test_a_stemdeck_on_the_rig_is_radla(self):
        self.assertEqual(return_target("127.0.0.1", OWN, RADLA), RADLA)
        self.assertEqual(return_target("192.168.8.10", OWN, RADLA), RADLA)

    def test_a_remote_stemdeck_gets_it_on_the_same_port(self):
        """A StemDeck machine listens where radla does: zita-from-truth.py n2j
        binds the truth's radla.zita-n2j port."""
        self.assertEqual(return_target(A3NUC2, OWN, RADLA), (A3NUC2, 55100))


class FollowingTheStemDeck(unittest.TestCase):
    def test_starts_at_radla(self):
        self.assertEqual(ReturnTarget(RADLA, OWN).current, RADLA)

    def test_a_remote_stemdeck_moves_it(self):
        t = ReturnTarget(RADLA, OWN)
        self.assertEqual(t.follow(A3NUC2), (A3NUC2, 55100))
        self.assertEqual(t.current, (A3NUC2, 55100))

    def test_the_same_stemdeck_again_moves_nothing(self):
        t = ReturnTarget(RADLA, OWN)
        t.follow(A3NUC2)
        self.assertIsNone(t.follow(A3NUC2))

    def test_a_local_stemdeck_moves_nothing_from_radla(self):
        self.assertIsNone(ReturnTarget(RADLA, OWN).follow("127.0.0.1"))

    def test_the_local_one_takes_it_back(self):
        t = ReturnTarget(RADLA, OWN)
        t.follow(A3NUC2)
        self.assertEqual(t.follow("127.0.0.1"), RADLA)

    def test_after_the_silence_it_is_radla_again(self):
        t = ReturnTarget(RADLA, OWN)
        t.follow(A3NUC2)
        self.assertEqual(t.follow(None), RADLA)
        self.assertIsNone(t.follow(None))

    def test_another_remote_one_takes_it_over(self):
        t = ReturnTarget(RADLA, OWN)
        t.follow(A3NUC2)
        self.assertEqual(t.follow(NOTEBOOK), (NOTEBOOK, 55100))

    def test_a_stale_remote_target_from_before_is_reset(self):
        """What the file said when Core came up: a remote StemDeck from before
        a reboot is not there until it says hello again."""
        t = ReturnTarget(RADLA, OWN, current=(A3NUC2, 55100))
        self.assertEqual(t.follow(None), RADLA)

    def test_a_file_that_already_says_radla_moves_nothing(self):
        self.assertIsNone(ReturnTarget(RADLA, OWN, current=RADLA).follow(None))


class TheFile(unittest.TestCase):
    def test_overrides_the_two_j2n_values_of_osc_env(self):
        self.assertEqual(return_env((A3NUC2, 55100)),
                         "A3_ZITA_J2N_HOST=192.168.8.20\nA3_ZITA_J2N_PORT=55100\n")

    def test_lives_beside_osc_env(self):
        self.assertEqual(RETURN_ENV_FILE, Path(".config/a3/zita-return.env"))

    def test_reads_back_what_was_written(self):
        with tempfile.TemporaryDirectory() as home:
            home = Path(home)
            point_zita_at((A3NUC2, 55100), home, restart=lambda: None)
            self.assertEqual(read_return_env(home), (A3NUC2, 55100))

    def test_a_missing_file_reads_as_nothing(self):
        with tempfile.TemporaryDirectory() as home:
            self.assertIsNone(read_return_env(Path(home)))

    def test_a_broken_file_reads_as_nothing(self):
        with tempfile.TemporaryDirectory() as home:
            path = Path(home) / RETURN_ENV_FILE
            path.parent.mkdir(parents=True)
            path.write_text("A3_ZITA_J2N_HOST=192.168.8.20\nA3_ZITA_J2N_PORT=x\n")
            self.assertIsNone(read_return_env(Path(home)))


class TheSwitch(unittest.TestCase):
    def test_writes_the_file_then_restarts(self):
        order = []
        with tempfile.TemporaryDirectory() as home:
            home = Path(home)

            def restart():
                order.append(("restart", read_return_env(home)))

            point_zita_at((A3NUC2, 55100), home, restart=restart)
        self.assertEqual(order, [("restart", (A3NUC2, 55100))])

    def test_restarts_the_user_unit(self):
        self.assertEqual(RESTART,
                         ("systemctl", "--user", "restart", "zita-j2n.service"))

    def test_the_restart_does_not_block_the_caller(self):
        """Core's serve loop reads every packet on one thread; systemctl
        waits for zita to stop and start again."""
        release = threading.Event()
        calls = []

        def slow_run(command, **_):
            calls.append(command)
            release.wait(5)

        thread = restart_zita(run=slow_run)
        self.assertTrue(thread.is_alive())
        release.set()
        thread.join(5)
        self.assertEqual(calls, [RESTART])


class AtStartUp(unittest.TestCase):
    """Core knows no StemDeck when it comes up, so the return goes to radla
    until one says hello -- a remote target from before a reboot must not
    survive it."""

    def start(self, home):
        restarts = []
        target = start_at_radla(RADLA, OWN, home,
                                restart=lambda: restarts.append(read_return_env(home)))
        return target, restarts

    def test_a_stale_remote_target_is_reset_and_zita_restarted(self):
        with tempfile.TemporaryDirectory() as home:
            home = Path(home)
            point_zita_at((A3NUC2, 55100), home, restart=lambda: None)
            target, restarts = self.start(home)
            self.assertEqual(read_return_env(home), RADLA)
        self.assertEqual(target.current, RADLA)
        self.assertEqual(restarts, [RADLA])

    def test_no_file_yet_is_written_without_a_restart(self):
        """No file means osc.env's values, which are radla's already."""
        with tempfile.TemporaryDirectory() as home:
            home = Path(home)
            _, restarts = self.start(home)
            self.assertEqual(read_return_env(home), RADLA)
        self.assertEqual(restarts, [])

    def test_a_file_that_says_radla_is_left_without_a_restart(self):
        with tempfile.TemporaryDirectory() as home:
            home = Path(home)
            point_zita_at(RADLA, home, restart=lambda: None)
            _, restarts = self.start(home)
        self.assertEqual(restarts, [])


if __name__ == "__main__":
    unittest.main()
