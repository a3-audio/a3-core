"""An install puts the shipped config in place, and keeps what it replaces.

The postinst copied ~/.local/share/a3-core/config into ~/.config with
``cp -rn``, which replaces nothing. A Core set up from a home that held an
older a3 kept the old REAPER template, OSC map, i3 config and user units, and
the new package changed none of them (a3nuc2, 2026-10-04). Now a missing file
is installed, and the parts with files that differ are offered in the
question a3-core/replace-config, all ticked. A file that is replaced is copied
to a backup folder first, under the same relative path, and named on the
output; one whose part was not chosen stays, and is named too.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

POSTINST = (Path(__file__).resolve().parents[2]
            / "platform-config/debian-x86_64/a3-core/DEBIAN/postinst")


TEMPLATES = POSTINST.parent / "templates"


def function(*names):
    bodies = []
    for name in names:
        body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(),
                         re.MULTILINE | re.DOTALL)
        if body is None:
            raise AssertionError(f"postinst defines no {name}()")
        bodies.append(body.group(0))
    return "\n".join(bodies)


INSTALLING = ("install_shipped_config", "config_group", "in_list")


def template(name):
    for block in TEMPLATES.read_text().split("\n\n"):
        if f"Template: a3-core/{name}" in block:
            return dict(re.findall(r"^([\w-]+): (.*)$", block, re.MULTILINE))
    return None


class InstallShippedConfig(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.src, self.dst, self.backup = root / "src", root / "dst", root / "backup"
        (self.src / "REAPER/ProjectTemplates").mkdir(parents=True)
        (self.src / "REAPER/ProjectTemplates/a3-reaper.RPP").write_text("new template\n")
        (self.src / "REAPER/presets").mkdir()
        (self.src / "REAPER/presets/with space.ini").write_text("preset\n")
        (self.src / "i3").mkdir()
        (self.src / "i3/config").write_text("new i3\n")
        self.dst.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def install(self, replace="reaper, i3, systemd, qjackctl, iem, other"):
        return subprocess.run(
            ["sh", "-c", f"set -e\n{function(*INSTALLING)}\n"
             'install_shipped_config "$1" "$2" "$3" "$4"', "sh",
             str(self.src), str(self.dst), str(self.backup), replace],
            capture_output=True, text=True, check=True)

    def groups(self):
        return subprocess.run(
            ["sh", "-c", f"{function('differing_config_groups', 'config_group')}\n"
             'differing_config_groups "$1" "$2"', "sh",
             str(self.src), str(self.dst)],
            capture_output=True, text=True, check=True).stdout.strip()

    def test_a_fresh_home_gets_every_file(self):
        self.install()
        for shipped in self.src.rglob("*"):
            if shipped.is_file():
                rel = shipped.relative_to(self.src)
                self.assertEqual(shipped.read_text(), (self.dst / rel).read_text(), rel)
        self.assertFalse(self.backup.exists(), "nothing was replaced, nothing to keep")

    def test_an_older_file_is_replaced_and_kept(self):
        (self.dst / "i3").mkdir()
        (self.dst / "i3/config").write_text("old i3\n")
        done = self.install()
        self.assertEqual("new i3\n", (self.dst / "i3/config").read_text())
        self.assertEqual("old i3\n", (self.backup / "i3/config").read_text())
        self.assertIn("i3/config", done.stdout)

    def test_an_unchanged_file_is_neither_backed_up_nor_named(self):
        (self.dst / "i3").mkdir()
        (self.dst / "i3/config").write_text("new i3\n")
        done = self.install()
        self.assertFalse((self.backup / "i3/config").exists())
        self.assertNotIn("i3/config", done.stdout)

    def test_files_the_package_does_not_ship_are_left_alone(self):
        (self.dst / "REAPER").mkdir()
        (self.dst / "REAPER/reaper.ini").write_text("this machine's audio device\n")
        self.install()
        self.assertEqual("this machine's audio device\n",
                         (self.dst / "REAPER/reaper.ini").read_text())


    def test_a_part_not_chosen_is_kept_and_named(self):
        (self.dst / "i3").mkdir()
        (self.dst / "i3/config").write_text("old i3\n")
        done = self.install(replace="reaper")
        self.assertEqual("old i3\n", (self.dst / "i3/config").read_text())
        self.assertFalse((self.backup / "i3/config").exists())
        self.assertIn("kept the local ~/.config/i3/config", done.stdout)

    def test_a_missing_file_is_installed_whatever_was_chosen(self):
        self.install(replace="")
        self.assertEqual("new i3\n", (self.dst / "i3/config").read_text())

    def test_only_parts_with_a_differing_file_are_offered(self):
        (self.dst / "i3").mkdir()
        (self.dst / "i3/config").write_text("old i3\n")
        (self.dst / "REAPER/ProjectTemplates").mkdir(parents=True)
        (self.dst / "REAPER/ProjectTemplates/a3-reaper.RPP").write_text("saved live\n")
        # presets/with space.ini is missing: installing it replaces nothing.
        self.assertEqual("reaper, i3", self.groups())

    def test_nothing_differing_offers_nothing(self):
        self.assertEqual("", self.groups())


class EveryShippedFileHasAPart(unittest.TestCase):
    """A file outside the named parts would be offered as "other files",
    which says nothing about what it is."""

    def test_every_shipped_file_falls_in_a_named_part(self):
        shipped = POSTINST.parents[1] / "home/aaa/.local/share/a3-core/config"
        for path in shipped.rglob("*"):
            if path.is_file():
                rel = str(path.relative_to(shipped))
                group = subprocess.run(
                    ["sh", "-c", f"{function('config_group')}\nconfig_group \"$1\"",
                     "sh", rel], capture_output=True, text=True).stdout.strip()
                self.assertNotEqual("other", group, rel)


class TheReplaceQuestion(unittest.TestCase):
    def test_it_is_a_multiselect_filled_at_install_time(self):
        question = template("replace-config")
        self.assertIsNotNone(question, "templates has no a3-core/replace-config")
        self.assertEqual("multiselect", question["Type"])
        self.assertEqual("${choices_c}", question["Choices-C"])

    def test_it_is_asked_at_a_priority_that_is_shown(self):
        self.assertRegex(POSTINST.read_text(),
                         r"db_input (high|critical) a3-core/replace-config")

    def test_every_offered_part_starts_ticked(self):
        """The default is everything (decided 2026-10-04): replacing nothing
        is what left a3nuc2 on a six month old REAPER."""
        body = function("ask_replace_question")
        self.assertIn('db_set a3-core/replace-config "$groups"', body)

    def test_an_answer_handed_over_by_the_installer_is_taken(self):
        """The a3-system installer asks first and preseeds the answer marked
        seen; the postinst must neither ask again nor reset it to everything
        (tried against a private debconf database, 2026-10-04)."""
        body = function("ask_replace_question")
        taken = body.index('if [ "x$RET" = "xtrue" ]; then')
        self.assertLess(taken, body.index("db_set a3-core/replace-config"))
        self.assertLess(taken, body.index("db_input high a3-core/replace-config"))

    def test_seen_is_cleared_after_asking(self):
        """Else the next plain install would take the old answer for the
        installer's and not ask."""
        body = function("ask_replace_question")
        after_asking = body[body.index("db_input high a3-core/replace-config"):]
        self.assertIn("db_fset a3-core/replace-config seen false", after_asking)

    def test_labels_carry_no_comma(self):
        """debconf reads a comma in Choices as the next choice."""
        for group in ("reaper", "i3", "systemd", "qjackctl", "iem", "other"):
            label = subprocess.run(
                ["sh", "-c", f"{function('config_group_label')}\n"
                 'config_group_label "$1"', "sh", group],
                capture_output=True, text=True).stdout.strip()
            self.assertNotIn(",", label, group)


class NoMoreCopyNoClobber(unittest.TestCase):
    def test_the_config_is_not_copied_with_cp_n(self):
        self.assertNotRegex(POSTINST.read_text(), r"^\s*cp\s+-\w*n\w*\s.*a3-core/config",
                            )


if __name__ == "__main__":
    unittest.main()
