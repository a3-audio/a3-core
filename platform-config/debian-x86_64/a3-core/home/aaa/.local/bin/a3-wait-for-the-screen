#!/bin/sh
#
# SPDX-FileCopyrightText: 2026 Patric Schmitz, Raphael Eismann
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Warte, bis der Bildschirm der ist, auf dem die App laufen soll.
#
# Das Problem, das dies loest, sieht wie ein Zufall aus und ist ein Rennen.
# Gemessen am Kaltstart vom 2026-09-19 (WindowReport, erste Zeile des Logs):
#
#   layout: window 1x1+0+0  screen 512x384  dpi 184.2  scale 2.00  logical 0x0
#
# Das Panel ist 1024x768 und wird nach rechts gedreht, also 768x1024. Beim
# Start meldete X aber 512x384 -- und daraus 184 dpi. JUCE nimmt die Skalierung
# einmal beim Start aus der DPI (round(dpi/96), juce_XWindowSystem_linux.cpp),
# also 2, und rechnet danach die ganze Oberflaeche in halber Groesse und
# vergroessert sie. Nichts holt diese Zahl je wieder ein: spaetere Zeilen im
# selben Log zeigen das Fenster bei 768x1024 und die Skalierung weiter bei 2.
#
# Der Grund fuer die 512x384 steht in der i3-Konfiguration:
#
#   exec sh -c 'sleep 2 && xrandr --output HDMI-2 --off && sleep 1 &&
#               xrandr --output HDMI-2 --mode 1024x768 --rotate right'
#
# X *antwortet* also lange, bevor der Ausgang ueberhaupt wieder an ist. Die
# bisherige Bedingung -- `xset q` beantwortet -- ist damit erfuellt, waehrend
# der Bildschirm noch aus ist.
#
# Warum keine feste Wartezeit: der Dienst lief an jenem Kaltstart bereits
# 20 Sekunden nach dem Boot an und war *immer noch* zu frueh. Eine Zahl, die
# heute reicht, ist die Zahl, die nach dem naechsten Plattentausch nicht mehr
# reicht -- und der Fehler kommt dann als "manchmal" zurueck, was ihn beim
# ersten Mal eine Woche gekostet hat.
#
# Gewartet wird deshalb auf die Groesse, die die Entscheidung tatsaechlich
# faellt: die von X gemeldete DPI. Unter 144 rundet JUCE auf 1.

DOUBLES_AT=144       # ab hier wird round(dpi/96) zu 2
STEP=0.2             # Sekunden je Runde
TRIES="${A3_SCREEN_TRIES:-300}"   # also hoechstens eine Minute

# In Runden gezaehlt und nicht in Sekunden, weil auf dieser Kiste kein `bc`
# liegt und die Shell nur ganze Zahlen kann.

# Ueberschreibbar, damit sich dieses Skript ohne X und ohne kaputten
# Bildschirm ausprobieren laesst.
QUERY="${A3_SCREEN_QUERY:-/usr/bin/xdpyinfo}"

dpi_now() {
    $QUERY 2>/dev/null | awk '/resolution:/ { split($2, r, "x"); print r[1]; exit }'
}

# Erst ueberhaupt ein X.
until /usr/bin/xset q >/dev/null 2>&1; do
    sleep 0.1
done

try=0
while [ "$try" -lt "$TRIES" ]; do
    dpi="$(dpi_now)"

    if [ -n "$dpi" ] && [ "$dpi" -lt "$DOUBLES_AT" ] 2>/dev/null; then
        # Einmal nachfassen: der Bildschirm wird beim Start ab- und wieder
        # angeschaltet, und dazwischen sieht er kurz richtig aus.
        sleep 1
        again="$(dpi_now)"
        if [ "$again" = "$dpi" ]; then
            echo "screen settled at ${dpi} dpi after $((try * 2 / 10)).$((try * 2 % 10))s" >&2
            exit 0
        fi
    fi

    sleep "$STEP"
    try=$((try + 1))
done

# Lieber eine haessliche Oberflaeche als gar keine: der Dienst startet
# trotzdem, aber die Zeile steht im Journal und erklaert, was man sieht.
echo "screen still at ${dpi:-unknown} dpi after $((TRIES / 5))s -- starting anyway;" \
     "the interface will come up at the wrong scale" >&2
exit 0
