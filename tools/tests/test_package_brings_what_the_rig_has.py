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

#: Every program the i3 config starts or binds, and the Debian package it
#: comes from. A program the config names but this map does not is a red test:
#: say where it comes from before shipping it.
#: unclutter's --hide-on-touch and --exclude-root are unclutter-xfixes' options.
PROGRAM_PACKAGES = {
    "dex": "dex",
    "unclutter": "unclutter-xfixes",
    "maim": "maim",
    "xclip": "xclip",
    "brightnessctl": "brightnessctl",
    "i3-sensible-terminal": "i3",
    "i3-nagbar": "i3",
    "i3-msg": "i3",
    "dmenu_run": "suckless-tools",
    "xset": "x11-xserver-utils",
    "xrandr": "x11-xserver-utils",
    "xss-lock": "xss-lock",
    "i3lock": "i3lock",
    "pactl": "pulseaudio-utils",
    "nm-applet": "network-manager-gnome",
    "firefox": "firefox-esr",
}

#: Packages a fresh install brings without naming them in Depends: the
#: installer runs apt-get without --no-install-recommends.
RECOMMENDED_BY_A_DEPENDS = {
    "suckless-tools": "i3 recommends it",
}

#: Gaps known and left open on purpose, each with the reason. Keep it short.
KNOWN_GAPS = {
    "firefox": "$mod+b; no browser in Depends, waiting on the maintainer",
}


def i3_programs():
    """The first word of every command an exec line or an exec binding runs,
    split at pipes, && and ;, with i3's --no-startup-id and quotes removed."""
    programs = set()
    for line in I3_CONFIG.read_text().splitlines():
        match = re.match(r"\s*(?:bindsym\s+\S+\s+)?exec(?:_always)?\s+(.*)", line)
        if match is None:
            continue
        command = match.group(1).replace("--no-startup-id", "").strip()
        command = command.strip("\"").replace("'", " ")
        for part in re.split(r"\|\||&&|\||;|\s--\s", command):
            words = part.split()
            while words and (words[0].startswith("$") or words[0] in ("-t", "-m", "-B")):
                words = words[1:]
            if words and not words[0].startswith("-"):
                programs.add(Path(words[0]).name)
    return programs


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
    """Whatever the i3 config runs, a fresh install brings (a3-core#65):
    nm-applet, xss-lock with i3lock and pactl failed silently every login."""

    def test_every_program_says_where_it_comes_from(self):
        unknown = i3_programs() - PROGRAM_PACKAGES.keys()
        self.assertEqual(set(), unknown)

    def test_every_program_is_installed_with_the_package(self):
        installed = depends() | RECOMMENDED_BY_A_DEPENDS.keys()
        missing = {program: PROGRAM_PACKAGES.get(program) for program in i3_programs()
                   if PROGRAM_PACKAGES.get(program) not in installed
                   and program not in KNOWN_GAPS}
        self.assertEqual({}, missing)

    def test_no_program_needs_a_conflicting_package(self):
        blocked = {program: PROGRAM_PACKAGES.get(program) for program in i3_programs()
                   if PROGRAM_PACKAGES.get(program) in conflicts()}
        self.assertEqual({}, blocked)

    def test_the_parser_sees_commands_after_a_pipe(self):
        self.assertIn("xclip", i3_programs())

    def test_a_known_gap_is_still_a_gap(self):
        for program in KNOWN_GAPS:
            self.assertIn(program, i3_programs(), f"{program} is gone: drop it from KNOWN_GAPS")
            self.assertNotIn(PROGRAM_PACKAGES[program], depends())


def conflicts():
    line = next(l for l in CONTROL.read_text().splitlines() if l.startswith("Conflicts:"))
    return {entry.strip() for entry in line.split(":", 1)[1].split(",")}


class NothingNeedsWhatCannotBeInstalled(unittest.TestCase):
    """rtirq-init is not in the Debian archive, and nm-applet needs
    network-manager, which the package conflicts with (a3-core#65)."""

    def test_no_rtirq_config_is_shipped(self):
        self.assertFalse((PACKAGE / "etc/rtirq.conf").exists())
        entries = [line.split() for line in
                   (PACKAGE / "DEBIAN/conffiles").read_text().splitlines()]
        self.assertNotIn(["/etc/rtirq.conf"], entries)

    def test_an_upgrade_takes_the_old_rtirq_config_away(self):
        """dpkg keeps a conffile the new version no longer ships; the flag
        removes it on the upgrade, or keeps it as .dpkg-bak if it was edited."""
        entries = [line.split() for line in
                   (PACKAGE / "DEBIAN/conffiles").read_text().splitlines()]
        self.assertIn(["remove-on-upgrade", "/etc/rtirq.conf"], entries)

    def test_the_i3_config_starts_no_nm_applet(self):
        self.assertIn("network-manager", conflicts())
        self.assertNotRegex(I3_CONFIG.read_text(), r"(?m)^\s*exec\b.*\bnm-applet\b")


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
