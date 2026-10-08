"""A fresh install brings what the rig only has because it was set up by hand.

limits.d/audio.conf gives @audio realtime priority, but nothing put `aaa` in
the group; the shipped i3 config starts and binds programs no Depends entry
installs (a3-core#65).
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config/debian-x86_64/a3-core"
POSTINST = PACKAGE / "DEBIAN/postinst"
CONTROL = PACKAGE / "DEBIAN/control"
I3_CONFIG = PACKAGE / "home/aaa/.local/share/a3-core/config/i3/config"

#: Programs the i3 config runs, and the Debian package each comes from.
#: unclutter's --hide-on-touch and --exclude-root are unclutter-xfixes' options.
I3_PROGRAMS = {
    "dex": "dex",
    "unclutter": "unclutter-xfixes",
    "maim": "maim",
    "xclip": "xclip",
    "brightnessctl": "brightnessctl",
}


def depends():
    line = next(l for l in CONTROL.read_text().splitlines() if l.startswith("Depends:"))
    return {alt.split("(")[0].strip()
            for entry in line.split(":", 1)[1].split(",") for alt in entry.split("|")}


def function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(),
                     re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


class TheI3ToolsAreInstalled(unittest.TestCase):
    def test_every_named_program_is_still_in_the_config(self):
        config = I3_CONFIG.read_text()
        for program in I3_PROGRAMS:
            self.assertRegex(config, rf"\b{program}\b", program)

    def test_their_packages_are_in_depends(self):
        missing = {p: pkg for p, pkg in I3_PROGRAMS.items() if pkg not in depends()}
        self.assertEqual({}, missing)


class AaaJoinsTheAudioGroup(unittest.TestCase):
    """Driven with fake `id` and `usermod`, never the real ones."""

    def join(self, groups):
        with tempfile.TemporaryDirectory() as tmp:
            bin_dir = Path(tmp)
            log = bin_dir / "usermod.log"
            (bin_dir / "id").write_text(f'#!/bin/sh\necho "{groups}"\n')
            (bin_dir / "usermod").write_text(f'#!/bin/sh\necho "$@" >> {log}\n')
            for fake in ("id", "usermod"):
                (bin_dir / fake).chmod(0o755)
            subprocess.run(["sh", "-c", f"set -e\n{function('join_audio_group')}\n"
                            'join_audio_group aaa'],
                           env={"PATH": f"{bin_dir}:/usr/bin:/bin"},
                           check=True, capture_output=True, text=True)
            return log.read_text() if log.exists() else ""

    def test_a_user_outside_the_group_is_added(self):
        self.assertEqual("-aG audio aaa\n", self.join("aaa sudo"))

    def test_a_member_is_left_alone(self):
        self.assertEqual("", self.join("aaa audio sudo"))

    def test_the_postinst_adds_the_app_user(self):
        self.assertRegex(POSTINST.read_text(), r'(?m)^join_audio_group "\$APPUSER"')


if __name__ == "__main__":
    unittest.main()
