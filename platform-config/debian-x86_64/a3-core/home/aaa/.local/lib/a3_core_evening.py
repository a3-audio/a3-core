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

import re

#: Was Core zwar weitergibt, aber nicht zurückspielt: der Crossfade steht in
#: a3_core_state. Alles andere, was Core weitergibt (`relayed_to` in der einen
#: Wahrheit), ist ein Wert, den REAPER hält -- genau das, was zurückkommt.
#: Lampen, Schalter, der Filtermodus und die Positionen gibt Core nicht weiter,
#: sondern sagt sie an oder hört sie nur; VU, Beat und /state/recall sind
#: Verkehr. Die Regel steht damit in der Wahrheit und nicht in einer Liste von
#: Präfixen, die beim Umbenennen von /fx nach /filter (2026-09-30) den Filter
#: still verloren und den Kanalschalter als Wert zurückgespielt hätte.
NOT_REPLAYED = frozenset(("channel.3d",))

#: Addresses renamed since an evening may have been written: FX send / FX
#: return became aux send / aux return on 2026-10-01, wire included. An old
#: evening still says fx and comes back under the new name instead of being
#: dropped as an address the truth no longer has.
RENAMED = ((re.compile(r"^/channel/(\d+)/fx-send$"), r"/channel/\1/aux-send"),
           (re.compile(r"^/master/fx-return$"), "/master/aux-return"))


def _current_name(address):
    for old, new in RENAMED:
        if old.match(address):
            return old.sub(new, address)
    return address


def evening_state(relayed):
    """Was Core zuletzt weitergegeben hat, als Daten, die json schreiben kann."""
    return {"values": dict(relayed.messages())}


def replayable(truth, state):
    """Die Werte aus `state`, die beim Start zurückgespielt werden dürfen.

    Jede Form, in der eine Datei ankommen kann, ist eine Form, in der sie
    ankommen darf: von einer älteren Version geschrieben, von Hand bearbeitet,
    leer. Keine davon darf werfen -- eine Bequemlichkeit, die den Start
    verhindert, ist schlimmer als keine.
    """
    values = (state or {}).get("values") or {}
    if not isinstance(values, dict):
        return

    addresses = truth.addresses()
    for address, value in values.items():
        if not isinstance(address, str):
            continue
        address = _current_name(address)
        found = truth.match(address)
        if found is None:
            continue
        key = found[0]
        if not addresses[key].get("relayed_to") or key in NOT_REPLAYED:
            continue

        yield address, value
