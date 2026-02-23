#!/usr/bin/env bash
set -euo pipefail

# Run as root
if [ "$(id -u)" -ne 0 ]; then
  echo "Bitte als root / mit sudo ausführen."
  exit 1
fi

# Config
KEY_URL="https://a3-audio.github.io/a3-core/KEY.gpg"
KEYRING="/usr/share/keyrings/a3-core-archive-keyring.gpg"
SOURCES_FILE_A3="/etc/apt/sources.list.d/a3-core.sources"
SOURCES_FILE_DEBIAN="/etc/apt/sources.list.d/debian.sources"
SOURCES_FILE_DEBIAN_BACKUP="/etc/apt/sources.list.d/debian.sources.bak"
REPO_URI="https://a3-audio.github.io/a3-core/"
# Note: we intentionally write 'Suites: ./' and leave Components empty per your request.

echo "1/4: Paketlisten aktualisieren und Werkzeuge installieren..."
apt-get update
apt-get install -y --no-install-recommends wget gnupg ca-certificates

echo "2/4: GPG-Key herunterladen und als Keyring installieren..."
if command -v curl >/dev/null 2>&1; then
  curl -fsSL "$KEY_URL" | gpg --dearmor -o "$KEYRING"
else
  wget -qO- "$KEY_URL" | gpg --dearmor -o "$KEYRING"
fi
chmod 644 "$KEYRING"

echo "3/4: APT‑Quelle anlegen (deb822 .sources): $SOURCES_FILE_A3"
cat > "$SOURCES_FILE_A3" <<EOF
Types: deb
URIs: $REPO_URI
Suites: ./
Components:
Signed-By: $KEYRING
EOF
chmod 644 "$SOURCES_FILE_A3"

# Backup original debian.sources if present
if [ -f "$SOURCES_FILE_DEBIAN" ]; then
  cp "$SOURCES_FILE_DEBIAN" "$SOURCES_FILE_DEBIAN_BACKUP"
  echo "Backup: $SOURCES_FILE_DEBIAN_BACKUP"

  # Replace known Debian suite names (trixie/stable and their updates/security variants)
  tmpfile=$(mktemp /tmp/debian.sources.XXXXXX)
  sed -E '
    s/^(Suites:\s*)trixie( +trixie-updates)?/\1testing testing-updates/g;
    s/^(Suites:\s*)trixie-security/\1testing-security/g;
    s/^(Suites:\s*)stable( +stable-updates)?/\1testing testing-updates/g;
    s/^(Suites:\s*)stable-security/\1testing-security/g;
  ' "$SOURCES_FILE_DEBIAN" > "$tmpfile"

  # Atomically install the modified file with correct permissions
  install -m 644 "$tmpfile" "$SOURCES_FILE_DEBIAN"
  rm -f "$tmpfile"
else
  echo "Hinweis: $SOURCES_FILE_DEBIAN nicht gefunden — Transformation übersprungen."
fi

echo "Neue deb822-Quelle(n):"
[ -f "$SOURCES_FILE_DEBIAN" ] && cat "$SOURCES_FILE_DEBIAN" || echo "(keine Datei vorhanden)"

echo "4/4: Paketlisten aktualisieren und a3-core installieren..."
apt-get update
apt-get install -y a3-core

echo "Installation complete."

echo "Configure Network."
dpkg-reconfigure a3-core