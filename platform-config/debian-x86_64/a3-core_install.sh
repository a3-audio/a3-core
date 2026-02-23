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
SOURCES_FILE_DEBIAN="/etc/apt/sources.list"
SOURCES_FILE_DEBIAN_BACKUP="/etc/apt/sources.list.bak"
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

mv -f /etc/apt/sources.list /etc/apt/sources.list.bck

cat << 'EOF' > /etc/apt/sources.list.d/debian.sources
Types: deb deb-src
URIs: http://deb.debian.org/debian/
Suites: testing
Components: main non-free-firmware
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg

Types: deb deb-src
URIs: http://security.debian.org/debian-security/
Suites: testing-security
Components: main non-free-firmware
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg

Types: deb deb-src
URIs: http://deb.debian.org/debian/
Suites: testing-updates
Components: main non-free-firmware
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
EOF

echo "4/4: Paketlisten aktualisieren und a3-core installieren..."
apt update
apt full-upgrade -y
apt install -y a3-core

echo "Installation complete."

echo "Configure Network."
dpkg-reconfigure a3-core
