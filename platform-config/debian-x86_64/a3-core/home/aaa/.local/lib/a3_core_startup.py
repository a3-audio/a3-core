"""Was Core beim Start aussprechen muss, damit alle dasselbe meinen.

Core merkt sich über einen Neustart hinweg, was nur es selbst weiß: die drei
Schalter eines Kanals, den Filtermodus, den 3D-Crossfade (a3_core_state). Es
hat sie bisher aber nur weitergesagt, wenn einer sich *änderte* --
`set_filters()` hing allein am Umschalten, die Mute des PFL-Tracks am Drücken.

Nach einem Start heißt das: REAPER behält, was in seinem Projekt steht, und
das muss nicht sein, was Core sich gemerkt hat. Gemeldet am 2026-09-18:
*„a3-core: fx aktiv obwohl beim runterfahren inaktiv."* Der Filter lief, die
Lampe war dunkel, und beide hatten aus ihrer Sicht recht.

Die Regeln stehen hier statt in a3-core.py, damit sie geprüft werden können:
dieses Modul öffnet keine Sockets und kennt keine Dataclass -- es bekommt die
Kanäle gereicht und liest die Felder, die es beim Namen kennt.
"""


def filter_bypass_messages(channels, mode_name, hipass_slot, lopass_slot):
    """Welches Filter-Plugin auf welchem Kanal läuft.

    Dieselbe Rechnung, die `set_filters()` immer hatte: ohne FX ist beides
    umgangen, mit FX läuft das eine, das der Modus nennt. REAPER erwartet 1
    für „Plugin aktiv" und 0 für Bypass.
    """
    for channel in channels:
        for slot, bypassed in (
                (lopass_slot,
                 not channel.toggle_fx or mode_name == "high_pass"),
                (hipass_slot,
                 not channel.toggle_fx or mode_name == "low_pass")):
            yield (f"/track/{channel.track_input}/fx/{slot}/bypass",
                   float(not bypassed))


def pfl_mute_messages(channels):
    """Ob der Vorhör-Track eines Kanals stumm ist. PFL an heißt: nicht stumm."""
    for channel in channels:
        yield f"/track/{channel.track_pfl}/mute", float(not channel.toggle_pfl)


def remembered_reaper_messages(channels, mode_name, hipass_slot, lopass_slot):
    """Alles, was REAPER hören muss, um Cores Gedächtnis zu entsprechen."""
    yield from filter_bypass_messages(channels, mode_name, hipass_slot,
                                      lopass_slot)
    yield from pfl_mute_messages(channels)
