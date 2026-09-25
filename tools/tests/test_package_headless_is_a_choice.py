"""Whether a Core draws to a dummy screen is asked, not shipped.

The package used to install /etc/X11/xorg.conf.d/10-headless.conf -- X's
``dummy`` driver -- as a conffile. On a machine with a screen that is a black
monitor with everything running behind it, and because dpkg installs a
conffile that is new to the package even where the local copy was moved aside,
one update put it back on a machine that had got rid of it. See a3-core
issue #53.

Now the file ships outside /etc, and the postinst installs it only when the
question ``a3-core/headless-display`` is answered yes.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64" / "a3-core"
POSTINST = PACKAGE / "DEBIAN" / "postinst"
TEMPLATES = PACKAGE / "DEBIAN" / "templates"
SHIPPED = PACKAGE / "home" / "aaa" / ".local" / "share" / "a3-core" / "x11" / "10-headless.conf"


def template(name):
    for block in TEMPLATES.read_text().split("\n\n"):
        if f"Template: a3-core/{name}" in block:
            return dict(re.findall(r"^(\w+): (.*)$", block, re.MULTILINE))
    return None


class TheDummyScreenIsNotShippedActive(unittest.TestCase):
    def test_nothing_under_etc_drives_x_with_the_dummy_driver(self):
        active = [p for p in (PACKAGE / "etc").rglob("*.conf")
                  if re.search(r'Driver\s+"dummy"', p.read_text())]
        self.assertEqual([], active)

    def test_the_config_ships_where_the_postinst_finds_it(self):
        self.assertTrue(SHIPPED.is_file())
        self.assertRegex(SHIPPED.read_text(), r'Driver\s+"dummy"')


class TheQuestion(unittest.TestCase):
    def test_it_is_a_boolean_that_defaults_to_no(self):
        question = template("headless-display")
        self.assertIsNotNone(question, "templates has no a3-core/headless-display")
        self.assertEqual("boolean", question["Type"])
        self.assertEqual("false", question["Default"])

    def test_the_postinst_asks_it_at_a_priority_that_is_shown(self):
        """Debian shows `high` and above by default; a `medium` question is
        answered with its default without anybody seeing it."""
        self.assertRegex(POSTINST.read_text(),
                         r"db_input (high|critical) a3-core/headless-display")


class ApplyingTheAnswer(unittest.TestCase):
    OTHER = 'Section "Device"\n  Identifier "Mine"\nEndSection\n'

    def apply(self, choice, target_text=None):
        body = re.search(r"^apply_headless_choice\(\) \{.*?^\}$",
                         POSTINST.read_text(), re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(body, "postinst defines no apply_headless_choice()")
        tmp = Path(tempfile.mkdtemp())
        target = tmp / "xorg.conf.d" / "10-headless.conf"
        target.parent.mkdir()
        if target_text is not None:
            target.write_text(target_text)
        subprocess.run(["sh", "-c", f"{body.group(0)}\n"
                        'apply_headless_choice "$1" "$2" "$3"', "sh",
                        choice, str(SHIPPED), str(target)], check=True)
        return target

    def test_yes_installs_the_shipped_config(self):
        target = self.apply("true")
        self.assertEqual(SHIPPED.read_text(), target.read_text())

    def test_yes_twice_is_still_one_config(self):
        target = self.apply("true", SHIPPED.read_text())
        self.assertEqual(SHIPPED.read_text(), target.read_text())
        self.assertEqual(["10-headless.conf"],
                         sorted(p.name for p in target.parent.iterdir()))

    def test_no_takes_the_shipped_config_out_of_x(self):
        """The machine the old package left it on is mended by answering no."""
        target = self.apply("false", SHIPPED.read_text())
        self.assertFalse(target.exists())
        self.assertTrue(target.with_name("10-headless.conf.off").exists())

    def test_no_leaves_a_config_somebody_wrote_alone(self):
        target = self.apply("false", self.OTHER)
        self.assertEqual(self.OTHER, target.read_text())

    def test_no_on_a_machine_without_it_does_nothing(self):
        target = self.apply("false")
        self.assertEqual([], list(target.parent.iterdir()))


if __name__ == "__main__":
    unittest.main()
