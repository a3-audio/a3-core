"""Zwischendurch sichern, damit ein Stromausfall nicht den Abend kostet.

Cores eigener Stand steht zwei Sekunden nach jeder Änderung auf der Platte
(a3_core_state). Was ein Stromausfall wirklich kostet, ist das
**REAPER-Projekt**: Gains, EQ, Positionen -- alles Kontinuierliche liegt dort
und wird nur beim Speichern geschrieben. Core kann das auslösen, und dieses
Modul entscheidet wann.

Zwei Regeln, und beide sind Verzicht:

- **Nur wenn sich etwas bewegt hat.** Ein stilles Pult löst kein Speichern
  aus; sonst schreibt die Maschine die ganze Nacht dieselbe Datei.
- **Höchstens alle `interval` Sekunden.** Ein Fader-Schwung sind hunderte
  Nachrichten und eine Absicht.

Es schreibt nichts selbst und kennt kein OSC: es sagt nur, ob es Zeit ist.
"""


class Snapshot:
    """Wann REAPER das Projekt speichern soll."""

    #: REAPERs „File: Save project". Über `t/action/@` in a3-core.ReaperOSC.
    SAVE_ACTION = "/action/40026"

    #: Wie oft nachgesehen wird, ob etwas ansteht. Billig: eine Frage an zwei
    #: Zahlen, und ein Speichern findet frühestens nach `interval` statt.
    CHECK_SECONDS = 20.0

    #: Fünf Minuten: lange genug, dass ein Abend nicht aus Speichervorgängen
    #: besteht, kurz genug, dass ein Stromausfall wenig kostet.
    DEFAULT_INTERVAL = 300.0

    def __init__(self, interval=DEFAULT_INTERVAL, now=0.0):
        self._interval = interval
        self._last_saved = now
        self._changed = False

    def changed(self):
        """Etwas ist durchgelaufen, das REAPER hält."""
        self._changed = True

    def due(self, now):
        """Ob jetzt gespeichert werden soll."""
        return self._changed and (now - self._last_saved) >= self._interval

    def saved(self, now):
        """Gespeichert: die Uhr läuft neu, und es liegt nichts mehr an."""
        self._last_saved = now
        self._changed = False
