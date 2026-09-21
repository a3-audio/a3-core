# SPDX-FileCopyrightText: 2026 Patric Schmitz, Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Jeder Handler, den a3-core.py definiert, muss auch angemeldet sein.

Die Sperrklinke zu einem Befund, der zweimal aufgeschrieben wurde, bevor ihn
jemand behob: `param_handler` stand über `osc_handler_channel`, sah aus wie
die allgemeinere Fassung davon — und war an keinen Dispatcher gebunden. Sein
Rumpf rief drei Funktionen, die es in der Datei nicht gibt
(`param_handler_channel`, `_master`, `_fx`), ein Aufruf wäre also ein
sofortiger NameError gewesen.

Dass nichts kaputtging, war der Schaden: wer beim Lesen diesen Pfad für den
lebenden hielt, suchte den Fehler an der falschen Stelle. Er las außerdem
`words[4]` als Parameter, wo der lebende Handler `words[3]` liest — ein Rest
aus einem früheren Adressschema.

Gemessen statt geglaubt: dieser Test liest die Quelle, nicht ein Verzeichnis
von Namen. Ein neuer Handler, den niemand anmeldet, fällt damit beim nächsten
Lauf auf und nicht in einem halben Jahr.
"""

import ast
import builtins
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "platform-config" / "debian-x86_64" / "a3-core" / "home"
        / "aaa" / ".local" / "bin" / "a3-core.py")

#: Handler, die mit Absicht nicht angemeldet sind, mit dem Grund.
#:
#: Leer, und das ist die Aussage. Wer hier etwas einträgt, schreibt den Grund
#: dazu -- ein Handler ohne Anmeldung ist entweder ein vergessener Draht oder
#: toter Code, und beides will benannt sein.
UNMAPPED_ON_PURPOSE = {}


def _tree():
    return ast.parse(CORE.read_text())


def _defined_handlers(tree):
    return {node.name for node in ast.walk(tree)
            if isinstance(node, ast.FunctionDef) and node.name.endswith("handler")}


def _mapped_handlers(tree):
    """Jeder Name, der einem dispatcher.map() oder set_default_handler()
    als Argument mitgegeben wird."""
    mapped = set()
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Attribute):
            continue
        if node.func.attr not in ("map", "set_default_handler"):
            continue
        for arg in node.args:
            if isinstance(arg, ast.Name):
                mapped.add(arg.id)
    return mapped


class EveryHandlerIsReachable(unittest.TestCase):
    def test_the_source_is_where_we_think_it_is(self):
        self.assertTrue(CORE.is_file(), CORE)

    def test_every_defined_handler_is_mapped(self):
        tree = _tree()
        defined = _defined_handlers(tree)
        mapped = _mapped_handlers(tree)

        self.assertTrue(defined, "keinen einzigen Handler gefunden -- Test kaputt")

        orphans = sorted(defined - mapped - set(UNMAPPED_ON_PURPOSE))
        self.assertEqual(
            [], orphans,
            "an keinen Dispatcher gebunden: %s. Entweder anmelden, oder "
            "entfernen, oder mit Grund in UNMAPPED_ON_PURPOSE eintragen."
            % ", ".join(orphans))

    def test_nothing_is_excused_that_does_not_exist(self):
        # Eine Ausnahme für einen Handler, den es nicht mehr gibt, ist eine
        # Ausnahme, die beim nächsten Mal schweigt.
        defined = _defined_handlers(_tree())
        for name in UNMAPPED_ON_PURPOSE:
            self.assertIn(name, defined, name)


class NothingArrivesUnseen(unittest.TestCase):
    """Und was auf kein map() passt, muss trotzdem aufgeschrieben werden.

    python-osc verschluckt eine Adresse ohne passendes Muster stillschweigend.
    Der Feedback-Dispatcher hatte seit jeher einen Auffang, der Hauptport
    nicht -- also war alles, was ein Bediengeraet an einer unbedienten Adresse
    sendet, von Stille nicht zu unterscheiden.
    """

    def test_the_main_dispatcher_has_a_catch_all(self):
        tree = _tree()
        defaults = [node for node in ast.walk(tree)
                    if isinstance(node, ast.Call)
                    and isinstance(node.func, ast.Attribute)
                    and node.func.attr == "set_default_handler"]

        targets = {node.func.value.id for node in defaults
                   if isinstance(node.func.value, ast.Name)}

        self.assertIn("dispatcher", targets,
                      "der Hauptport hat keinen Auffang -- was auf kein map() "
                      "passt, verschwindet dann spurlos")
        self.assertIn("feedback_dispatcher", targets)


class TheHandlersCallWhatExists(unittest.TestCase):
    """Und was sie rufen, muss es geben.

    `param_handler` rief drei Funktionen, die nirgends definiert sind. Kein
    Linter lief hier, und aufgefallen ist es erst beim Lesen -- zweimal, von
    zwei verschiedenen Seiten.
    """

    def test_no_handler_calls_a_function_that_is_not_there(self):
        tree = _tree()
        defined = {node.name for node in ast.walk(tree)
                   if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))}
        imported = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.ImportFrom):
                imported.update(a.asname or a.name for a in node.names)
            elif isinstance(node, ast.Import):
                imported.update((a.asname or a.name).split(".")[0] for a in node.names)

        missing = []
        for node in ast.walk(tree):
            if not isinstance(node, ast.FunctionDef) or not node.name.endswith("handler"):
                continue
            for call in ast.walk(node):
                if not isinstance(call, ast.Call) or not isinstance(call.func, ast.Name):
                    continue
                name = call.func.id
                if (name in defined or name in imported
                        or hasattr(builtins, name)):
                    continue
                missing.append("%s ruft %s" % (node.name, name))

        self.assertEqual([], sorted(set(missing)), "; ".join(sorted(set(missing))))


if __name__ == "__main__":
    unittest.main()
