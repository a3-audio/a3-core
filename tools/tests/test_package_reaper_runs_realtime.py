"""REAPER's audio worker threads run realtime on PipeWire (2026-10-14).

pipewire-jack (1.6.9, still in master) answers
jack_client_max_real_time_priority() from an uninitialised variable: its
client thread utils have no get_rt_range. REAPER read -6 and ran its worker
threads without realtime scheduling. A small library preloaded into REAPER
answers what pipewire-jack intends instead: 82 for the client limit, 77 for
the workers, both under PipeWire's data loop (83).

The package is built by dpkg-deb alone, on GitHub and on the LAN builder, so
the postinst compiles the shipped source with gcc (build-essential is in
Depends) to where the REAPER unit preloads it from.
"""

import os
import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

from tools.tests.paths import DEBIAN, PACKAGE

SOURCE = PACKAGE / "home/aaa/.local/share/a3-core/src/jack-rt-priority.c"
REAPER_UNIT = PACKAGE / "home/aaa/.local/share/a3-core/config/systemd/user/a3-reaper.service"
POSTINST = DEBIAN / "postinst"
LIBRARY = ".local/lib/a3/libjack-rt-priority.so"
DATA_LOOP_PRIORITY = 83


def postinst_function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(),
                     re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


def compile_shim(source, out_dir):
    library = Path(out_dir) / "libjack-rt-priority.so"
    subprocess.run(
        ["sh", "-c", f'{postinst_function("build_jack_rt_shim")}\n'
                     'build_jack_rt_shim "$1" "$2"', "sh", str(source), str(library)],
        check=True, capture_output=True, text=True)
    return library


def answers(library, env_value=None):
    """(worker, max) priorities the library answers, asked from a fresh
    process so its getenv sees the environment given."""
    program = textwrap.dedent(f"""
        import ctypes
        lib = ctypes.CDLL({str(library)!r})
        print(lib.jack_client_real_time_priority(None),
              lib.jack_client_max_real_time_priority(None))
    """)
    env = dict(os.environ)
    env.pop("A3_JACK_RT_PRIORITY", None)
    if env_value is not None:
        env["A3_JACK_RT_PRIORITY"] = env_value
    done = subprocess.run(["python3", "-I", "-c", program], env=env,
                          check=True, capture_output=True, text=True)
    worker, maximum = done.stdout.split()
    return int(worker), int(maximum)


@unittest.skipUnless(shutil.which("gcc"), "no gcc here")
class TheShim(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.library = compile_shim(SOURCE, cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_it_answers_77_and_82(self):
        self.assertEqual(answers(self.library), (77, 82))

    def test_both_stay_under_the_data_loop(self):
        worker, maximum = answers(self.library)
        self.assertLess(worker, DATA_LOOP_PRIORITY)
        self.assertLess(maximum, DATA_LOOP_PRIORITY)

    def test_the_environment_moves_the_workers(self):
        self.assertEqual(answers(self.library, "60"), (60, 65))

    def test_nonsense_in_the_environment_is_77(self):
        self.assertEqual(answers(self.library, "banana"), (77, 82))
        self.assertEqual(answers(self.library, "120"), (77, 82))


class TheBuild(unittest.TestCase):
    def test_a_failed_build_warns_and_the_install_goes_on(self):
        with tempfile.TemporaryDirectory() as tmp:
            broken = Path(tmp) / "broken.c"
            broken.write_text("this is not C\n")
            done = subprocess.run(
                ["sh", "-c", f'set -e\n{postinst_function("build_jack_rt_shim")}\n'
                             'build_jack_rt_shim "$1" "$2"\necho went-on', "sh",
                 str(broken), str(Path(tmp) / "lib/libjack-rt-priority.so")],
                capture_output=True, text=True)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertIn("went-on", done.stdout)
            self.assertIn("realtime", done.stderr)

    def test_the_postinst_builds_it_where_reaper_preloads_it(self):
        text = POSTINST.read_text()
        self.assertIn('build_jack_rt_shim "${USER_HOME}/.local/share/a3-core/src/'
                      f'jack-rt-priority.c" "${{USER_HOME}}/{LIBRARY}"', text)
        build = text.index("build_jack_rt_shim \"${USER_HOME}")
        chown = text.index('chown -R "${APPUSER}:${APPUSER}" "${USER_HOME}/.local"')
        self.assertLess(build, chown, "built after the chown, it stays root's")


class TheUnitPreloadsIt(unittest.TestCase):
    def test_reaper_runs_with_the_shim(self):
        self.assertIn(f"Environment=LD_PRELOAD=%h/{LIBRARY}",
                      REAPER_UNIT.read_text().splitlines())


if __name__ == "__main__":
    unittest.main()
