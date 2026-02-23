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
SOURCES_FILE="/etc/apt/sources.list.d/a3-core.sources"
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

echo "3/4: APT‑Quelle anlegen (deb822 .sources): $SOURCES_FILE"
cat > "$SOURCES_FILE" <<EOF
Types: deb
URIs: $REPO_URI
Suites: ./
Components:
Signed-By: $KEYRING
EOF
chmod 644 "$SOURCES_FILE"

echo "4/4: Paketlisten aktualisieren und a3-core installieren..."
apt-get update
apt-get install -y a3-core

echo "Installation complete."