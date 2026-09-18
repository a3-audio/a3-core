"""Der Abend, wie Core ihn gesehen hat, für den nächsten Start.

REAPER hält jeden kontinuierlichen Wert -- Gain, EQ, Volume, Filter -- und
schreibt ihn beim Beenden in seine Vorlage. Ein Stromausfall überspringt das
Beenden, und der Abend ist weg. Ein „Projekt speichern" zwischendurch gibt es
hier nicht: REAPER läuft aus einer Vorlage und hat kein Projektfile, also
öffnete das einen Dialog über dem Panel (2026-09-18 geprüft).

Core sieht die Werte aber alle: `Relayed` hält, was zuletzt durchgelaufen ist,
und das ist keine zweite Meinung, sondern REAPERs eigene letzte Aussage. Das
wird mitgeschrieben und beim Start denselben Weg zurückgeschickt, den eine
Nachricht vom Pult nimmt -- durch Cores eigenen Parameter-Handler, der daraus
wieder REAPER-Nachrichten macht.

**Was nicht zurückkommt, ist der eigentliche Inhalt dieses Moduls:**

- **Lampen** sind Status, keine Einstellung. Sie ergeben sich aus den Flags.
- **Schalter, Filtermodus und Crossfade** stehen schon in a3_core_state. Zwei
  Kopien derselben Sache können sich widersprechen.
- **Positionen** sind das Einzige, was sich ohne Hand ändert: eine Trajektorie
  schreibt sie fortlaufend. Beim Start zurückgespielt setzten sie den Klang an
  eine Stelle, die niemand gewählt hat.
- **Alles, was Core gar nicht setzt** (VU, Beat, /state/recall) ist keine
  Einstellung, sondern Verkehr.
"""

#: Adressen, die Core setzen kann. Alles andere ist Verkehr.
REPLAYABLE_PREFIXES = ("/channel/", "/master/", "/fx/")

#: Was innerhalb dieser Präfixe trotzdem nicht zurückgespielt wird, als
#: letztes Segment der Adresse. Die Gründe stehen oben.
NOT_REPLAYED = frozenset((
    "led",          # eine Lampe ist Status
    "pfl", "fx",    # Schalter: a3_core_state
    "3d",           # Crossfade: a3_core_state
    "mode",         # /fx/mode: a3_core_state
    "azimuth", "elevation",   # Positionen: siehe oben
))


def evening_state(relayed):
    """Was Core zuletzt weitergegeben hat, als Daten, die json schreiben kann."""
    return {"values": dict(relayed.messages())}


def replayable(state):
    """Die Werte aus `state`, die beim Start zurückgespielt werden dürfen.

    Jede Form, in der eine Datei ankommen kann, ist eine Form, in der sie
    ankommen darf: von einer älteren Version geschrieben, von Hand bearbeitet,
    leer. Keine davon darf werfen -- eine Bequemlichkeit, die den Start
    verhindert, ist schlimmer als keine.
    """
    values = (state or {}).get("values") or {}
    if not isinstance(values, dict):
        return

    for address, value in values.items():
        if not isinstance(address, str):
            continue
        if not address.startswith(REPLAYABLE_PREFIXES):
            continue
        if any(segment in NOT_REPLAYED for segment in address.split("/")[2:]):
            continue

        yield address, value
