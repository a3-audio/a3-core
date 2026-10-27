#!/bin/bash

# Liste der HDMI-Anschlüsse, die überprüft werden sollen
HDMI_PORTS=(
    "/sys/class/drm/card0-HDMI-A-1/status"
    "/sys/class/drm/card0-HDMI-A-2/status"
    "/sys/class/drm/card0-HDMI-A-3/status"
    "/sys/class/drm/card1-HDMI-A-4/status"
    "/sys/class/drm/card1-HDMI-A-5/status"
    "/sys/class/drm/card0-DP-1/status"
    "/sys/class/drm/card0-DP-2/status"
    "/sys/class/drm/card1-DP-3/status"
    "/sys/class/drm/card1-DP-4/status"
    "/sys/class/drm/card1-DP-5/status"
    "/sys/class/drm/card1-DP-6/status"
)

# Funktion zum Überprüfen der HDMI-Verbindungen
check_hdmi_connections() {
    local connected=0
    
    echo "Starte HDMI-Verbindungscheck"
    
    # Schleife durch alle HDMI-Anschlüsse
    for port in "${HDMI_PORTS[@]}"; do
        echo "Prüfe Anschluss: $port"
        
        # Überprüfen, ob die Datei existiert
        if [ ! -f "$port" ]; then
            echo "FEHLER: Anschluss-Datei nicht gefunden: $port"
            continue
        fi

        # Status auslesen
        HDMI_STATUS=$(cat "$port")
        echo "Status von $port: $HDMI_STATUS"

        # Verbindungsstatus prüfen
        if [ "$HDMI_STATUS" = "connected" ]; then
            connected=$((connected + 1))
            echo "Verbunden: $port"
        fi
    done

    # Ausgabe der Gesamtergebnisse
    echo "Anzahl verbundener HDMI-Anschlüsse: $connected"
    
    # Aktionen basierend auf Verbindungsstatus
    if [ $connected -gt 0 ]; then
        echo "Mindestens ein HDMI-Anschluss ist verbunden"
        
        # Dummy-Treiber-Konfiguration deaktivieren
        if [ -f "/etc/X11/xorg.conf.d/10-headless.conf" ]; then
            echo "Deaktiviere Dummy-Treiber-Konfiguration"
            sudo mv /etc/X11/xorg.conf.d/10-headless.conf /etc/X11/xorg.conf.d/10-headless.conf.bak
        else
            echo "Keine Dummy-Treiber-Backup-Konfiguration gefunden"
        fi
    else
        echo "Kein HDMI-Anschluss verbunden"
        
        # Dummy-Treiber-Konfiguration aktivieren
        if [ -f "/etc/X11/xorg.conf.d/10-headless.conf.bak" ]; then
            echo "Aktiviere Dummy-Treiber-Konfiguration"
            sudo mv /etc/X11/xorg.conf.d/10-headless.conf.bak /etc/X11/xorg.conf.d/10-headless.conf
            #sudo X :0 -config /etc/X11/xorg.conf.d/10-headless.conf &
	fi
    fi
}

# Funktion aufrufen
check_hdmi_connections
