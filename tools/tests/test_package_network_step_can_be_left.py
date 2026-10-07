"""The network step asks, can be left with ESC, and never writes blind.

a3-core#68: an upgrade from 1.0.0 rewrote /etc/systemd/network/a3.network on
a machine where nobody wanted it. Two faults together:

- The postinst took a set `seen` flag on a3-core/configure-network as the
  a3-system installer's answer and did not ask. Package 1.0.0 had shown the
  same question, which marks it seen, and the flag outlived it -- with the
  answer `true`.
- Every db_go in the network dialogs ended in `|| true`, and debconf was never
  told the script can go back (`db_capb backup`), so ESC wrote the pre-filled
  values anyway.

Now the installer marks its answers explicitly (a3-core/preseeded), the
postinst takes them only then and clears the marker, and an ESC anywhere in
the network dialogs ends the network step without writing anything; the rest
of the postinst carries on. a3.network is written only when it changes, and
the file it replaces is kept beside it.
"""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[2] / "platform-config" / "debian-x86_64" / "a3-core"
POSTINST = PACKAGE / "DEBIAN" / "postinst"
TEMPLATES = PACKAGE / "DEBIAN" / "templates"

# The postinst's own functions the network step is made of.
FUNCTIONS = ("forget_retired_network", "ask_address_questions",
             "other_wired_interface", "write_network_config",
             "ask_bridge_question", "disable_ifupdown_for",
             "installer_answered_network", "ask_configure_network",
             "network_left", "network_step")

# debconf, as files in $DB: a question's value in $DB/<name>, a flag in
# $DB/<name>.<flag>. db_go "answers" every question put since the last go
# from $ANSWERS/<name>, the way a user would, unless $ESC_AT is among them:
# then it answers nothing and returns 30, as debconf does for ESC/back.
# Defined after the postinst's functions, so these stand in for the
# confmodule and for the steps that need root or the real home.
STUBS = r"""
name () { echo "${1#a3-core/}"; }
db_capb () { :; }
db_subst () { :; }
db_get () { RET="$(cat "$DB/$(name "$1")" 2>/dev/null || true)"; }
db_set () { printf '%s' "$2" > "$DB/$(name "$1")"; echo "set $1 $2"; }
db_fget () { RET="$(cat "$DB/$(name "$1").$2" 2>/dev/null || echo false)"; }
db_fset () { printf '%s' "$3" > "$DB/$(name "$1").$2"; echo "fset $1 $2 $3"; }
db_reset () {
  echo "reset $1"
  if [ "$1" = a3-core/configure-network ]; then printf false > "$DB/configure-network"
  else rm -f "$DB/$(name "$1")"; fi
}
QUEUE=""
db_input () { echo "input $1 $2"; QUEUE="$QUEUE $2"; }
db_go () {
  echo "go"
  asked="$QUEUE"; QUEUE=""
  if [ -n "$ESC_AT" ]; then
    case " $asked " in *" $ESC_AT "*) return 30 ;; esac
  fi
  for q in $asked; do
    [ -f "$ANSWERS/$(name "$q")" ] && cp "$ANSWERS/$(name "$q")" "$DB/$(name "$q")"
  done
  return 0
}
create_network_file () { mkdir -p "$HOME_DIR/.config/a3"; echo '{}' > "$HOME_DIR/.config/a3/network.json"; }
render_default_network () {
  echo "INTERFACE=eno1 ADDR=192.168.8.10/24 GATEWAY=192.168.8.1 DNS=192.168.8.1 BRIDGE_WITH=''"
}
enable_networkd () { echo "enable networkd"; }
"""


def function(name):
    body = re.search(rf"^{name}\(\) \{{.*?^\}}$", POSTINST.read_text(),
                     re.MULTILINE | re.DOTALL)
    if body is None:
        raise AssertionError(f"postinst defines no {name}()")
    return body.group(0)


class NetworkStep(unittest.TestCase):
    """Runs network_step against a temp dir standing in for
    /etc/systemd/network, /etc/network/interfaces and the user's home."""

    IFACES = "auto lo\niface lo inet loopback\n\nallow-hotplug eno1\niface eno1 inet dhcp\n"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.db, self.answers = root / "db", root / "answers"
        self.net, self.home = root / "network", root / "home"
        self.ifaces = root / "interfaces"
        for d in (self.db, self.answers, self.net, self.home):
            d.mkdir()
        self.ifaces.write_text(self.IFACES)

    def tearDown(self):
        self.tmp.cleanup()

    def store(self, **values):
        for key, value in values.items():
            (self.db / key.replace("_", "-")).write_text(value)

    def flag(self, question, flag, value):
        (self.db / f"{question}.{flag}").write_text(value)

    def user_answers(self, **values):
        for key, value in values.items():
            (self.answers / key.replace("_", "-")).write_text(value)

    def stored(self, question):
        path = self.db / question
        return path.read_text() if path.exists() else ""

    def run_step(self, esc_at=""):
        script = ("set -e\n" + "\n".join(function(n) for n in FUNCTIONS) + "\n"
                  + STUBS
                  + 'network_step "$NET" "$IFACES" "$HOME_DIR"\necho "after the network step"\n')
        done = subprocess.run(
            ["sh", "-c", script], capture_output=True, text=True,
            env={"PATH": "/usr/bin:/bin", "DB": str(self.db), "ANSWERS": str(self.answers),
                 "ESC_AT": esc_at, "NET": str(self.net), "IFACES": str(self.ifaces),
                 "HOME_DIR": str(self.home)})
        self.assertEqual(0, done.returncode, done.stderr)
        self.calls = done.stdout.splitlines()
        self.assertEqual("after the network step", self.calls[-1])
        return self.calls

    def a_machine_set_up_by_package_1_0_0(self):
        """configure-network answered yes and marked seen, the old address
        stored, no marker."""
        self.store(configure_network="true", install_default_network="false",
                   interface="eno1", address="192.168.8.10/24",
                   gateway="192.168.8.1", dns="192.168.8.1", bridge_with="")
        self.flag("configure-network", "seen", "true")

    def assertNothingWritten(self):
        self.assertEqual([], sorted(p.name for p in self.net.iterdir()))
        self.assertEqual(self.IFACES, self.ifaces.read_text())
        self.assertFalse((self.home / ".config/a3/network.json").exists())
        self.assertNotIn("enable networkd", self.calls)


class AStaleSeenFlagIsNotTheInstallersAnswer(NetworkStep):
    def test_the_question_is_asked(self):
        self.a_machine_set_up_by_package_1_0_0()
        calls = self.run_step()
        self.assertIn("input high a3-core/configure-network", calls)

    def test_enter_on_the_offered_no_writes_nothing(self):
        self.a_machine_set_up_by_package_1_0_0()
        self.run_step()
        self.assertEqual("false", self.stored("configure-network"))
        self.assertNothingWritten()

    def test_the_seen_flag_is_cleared(self):
        self.a_machine_set_up_by_package_1_0_0()
        self.run_step()
        self.assertEqual("false", self.stored("configure-network.seen"))


class TheInstallersMarkedAnswersAreTaken(NetworkStep):
    def preseed(self):
        self.store(configure_network="true", install_default_network="false",
                   interface="eno1", address="192.168.8.20/24",
                   gateway="192.168.8.1", dns="192.168.8.1", bridge_with="",
                   preseeded="true")
        for q in ("configure-network", "interface", "address", "gateway", "dns",
                  "bridge-with"):
            self.flag(q, "seen", "true")

    def test_nothing_is_asked(self):
        self.preseed()
        calls = self.run_step()
        self.assertFalse([c for c in calls if c.startswith("input")], calls)

    def test_the_answers_are_written(self):
        self.preseed()
        self.run_step()
        self.assertIn("Address=192.168.8.20/24", (self.net / "a3.network").read_text())

    def test_the_marker_is_cleared(self):
        self.preseed()
        self.run_step()
        self.assertEqual("false", self.stored("preseeded"))

    def test_a_marked_no_writes_nothing_and_clears_the_marker(self):
        self.store(configure_network="false", preseeded="true")
        self.run_step()
        self.assertNothingWritten()
        self.assertEqual("false", self.stored("preseeded"))

    def test_the_marker_is_a_template(self):
        self.assertIn("Template: a3-core/preseeded\nType: boolean\nDefault: false",
                      TEMPLATES.read_text())


class EscWritesNothing(NetworkStep):
    def a_user_who_says_yes(self):
        self.a_machine_set_up_by_package_1_0_0()
        self.user_answers(configure_network="true", address="10.0.0.5/24",
                          bridge_with="enp5s0")

    def test_esc_at_the_first_question(self):
        self.a_machine_set_up_by_package_1_0_0()
        self.run_step(esc_at="a3-core/configure-network")
        self.assertNothingWritten()

    def test_esc_at_the_address(self):
        self.a_user_who_says_yes()
        self.run_step(esc_at="a3-core/address")
        self.assertNothingWritten()

    def test_esc_at_the_interface(self):
        self.a_user_who_says_yes()
        self.run_step(esc_at="a3-core/interface")
        self.assertNothingWritten()

    def test_esc_at_the_bridge(self):
        self.a_user_who_says_yes()
        self.run_step(esc_at="a3-core/bridge-with")
        self.assertNothingWritten()

    def test_without_esc_the_answers_are_written(self):
        """The counter-check: the same user, no ESC, gets the network."""
        self.a_user_who_says_yes()
        self.run_step()
        self.assertIn("Address=10.0.0.5/24",
                      (self.net / "30-a3-bridge-address.network").read_text())
        self.assertIn("enable networkd", self.calls)

    def test_the_postinst_can_go_back(self):
        self.assertRegex(POSTINST.read_text(), r"\ndb_capb backup\b")

    def test_no_network_dialog_discards_what_db_go_says(self):
        for name in ("ask_address_questions", "ask_bridge_question",
                     "ask_configure_network"):
            self.assertNotRegex(function(name), r"db_go \|\| true", name)


class TheNetworkFileIsOnlyWhatWasConfigured(NetworkStep):
    """~/.config/a3/network.json carries the package's defaults (eno1, a
    bridge with enp5s0, .10): on a machine whose network was not configured,
    or was configured from other answers, it names another machine."""

    def test_not_written_when_the_network_is_left(self):
        self.a_machine_set_up_by_package_1_0_0()
        self.run_step()
        self.assertFalse((self.home / ".config/a3/network.json").exists())

    def test_not_written_from_asked_answers(self):
        self.a_machine_set_up_by_package_1_0_0()
        self.user_answers(configure_network="true", address="10.0.0.5/24")
        self.run_step()
        self.assertFalse((self.home / ".config/a3/network.json").exists())

    def test_written_for_the_default_network_it_describes(self):
        self.store(configure_network="true", install_default_network="true",
                   preseeded="true")
        self.run_step()
        self.assertTrue((self.home / ".config/a3/network.json").exists())


class TheUnitIsWrittenOnlyWhenItChanges(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, addr, with_=""):
        subprocess.run(["sh", "-c", function("write_network_config")
                        + '\nwrite_network_config "$@"', "sh", str(self.dir),
                        "eno1", with_, addr, "192.168.8.1", "192.168.8.1"], check=True)

    def backups(self):
        return sorted(p.name for p in self.dir.iterdir() if ".bak-" in p.name)

    def test_unchanged_content_is_not_rewritten(self):
        self.write("192.168.8.10/24")
        unit = self.dir / "a3.network"
        before = unit.stat()
        self.write("192.168.8.10/24")
        after = unit.stat()
        self.assertEqual((before.st_ino, before.st_mtime_ns), (after.st_ino, after.st_mtime_ns))
        self.assertEqual([], self.backups())

    def test_changed_content_keeps_the_old_file(self):
        unit = self.dir / "a3.network"
        unit.write_text("[Match]\nName=eno1\n\n[Network]\nAddress=192.168.8.30/24\n")
        self.write("192.168.8.10/24")
        backups = self.backups()
        self.assertEqual(1, len(backups), backups)
        self.assertTrue(backups[0].startswith("a3.network.bak-"))
        self.assertIn("192.168.8.30/24", (self.dir / backups[0]).read_text())
        self.assertIn("192.168.8.10/24", unit.read_text())

    def test_a_unit_the_other_variant_removes_is_kept_too(self):
        unit = self.dir / "a3.network"
        unit.write_text("[Match]\nName=eno1\n\n[Network]\nAddress=192.168.8.30/24\n")
        self.write("192.168.8.10/24", "enp5s0")
        self.assertFalse(unit.exists())
        self.assertEqual(1, len([b for b in self.backups() if b.startswith("a3.network.bak-")]))

    def test_a_backup_does_not_end_in_a_suffix_networkd_reads(self):
        (self.dir / "a3.network").write_text("old\n")
        self.write("192.168.8.10/24")
        for name in self.backups():
            self.assertFalse(name.endswith((".network", ".netdev", ".link")), name)


if __name__ == "__main__":
    unittest.main()
