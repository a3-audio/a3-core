#!/bin/bash
set -e

#### Install Reaper

echo "Installing reaper..."
REAPER_URL=$(curl -s https://www.reaper.fm/download.php | grep -oP 'files/[0-9]+\.x/reaper[0-9]+_linux_x86_64\.tar\.xz' | head -1)
if [ -z "$REAPER_URL" ]; then
  echo "ERROR: Could not find Reaper download URL"
  exit 1
fi
REAPER_URL="https://www.reaper.fm/${REAPER_URL}"
REAPER_FILE=$(basename "$REAPER_URL")
echo "Downloading $REAPER_URL"
wget "$REAPER_URL"
tar -xf "$REAPER_FILE"
cd reaper_linux_x86_64
./install-reaper.sh --install /home/aaa/.local/opt/ --quiet
cd ..
rm -rf reaper_linux_x86_64
rm "$REAPER_FILE"
mkdir -p /home/aaa/.local/bin
ln -sf /home/aaa/.local/opt/REAPER/reaper /home/aaa/.local/bin/reaper
# The REAPER configuration is no longer an archive. It is plain files in the
# package, and postinst installs them with `cp -rn` -- which does not clobber,
# where `unzip -o` did. That overwrote a live project with a six month old one
# on 2026-09-24, along with the plugin scan cache, which took the whole IEM
# suite out of REAPER until the search path was repaired.

#### Point REAPER at the plugin directories

# reaper.ini is the machine's -- audio device, window positions, everything
# set up once at the rig -- so the package does not ship it. Two keys in it
# are ours, though: REAPER does not search ~/.local/vst or ~/.local/clap
# unless told, and that is where the plugins below go.
#
# Set only those two. Shipping the whole file is what replaced a working
# configuration on 2026-09-24 and left REAPER hunting for the IEM suite down
# a path that existed on no machine.
#
# Deleting the keys and trusting REAPER's own defaults was tried and does not
# work: with vstpath removed, the IEM plugins in /usr/lib/vst3 went missing
# too, so the defaults are narrower than they look. Name the directories.

REAPER_INI=/home/aaa/.config/REAPER/reaper.ini
VSTPATH='/home/aaa/.local/vst/;/usr/lib/vst3/'
CLAPPATH='/home/aaa/.local/clap;/usr/lib/clap'

mkdir -p "$(dirname "$REAPER_INI")"
if [ ! -f "$REAPER_INI" ]; then
    # First install: REAPER has not written one yet. A stub is enough; REAPER
    # keeps these keys and adds its own on first exit.
    printf '[REAPER]\nvstpath=%s\nclap_path_linux-x86_64=%s\n' \
        "$VSTPATH" "$CLAPPATH" > "$REAPER_INI"
    echo "done: wrote plugin paths into a new $REAPER_INI"
else
    for pair in "vstpath=$VSTPATH" "clap_path_linux-x86_64=$CLAPPATH"; do
        key=${pair%%=*}
        if grep -q "^$key=" "$REAPER_INI"; then
            sed -i "s|^$key=.*|$pair|" "$REAPER_INI"
        else
            sed -i "0,/^\[REAPER\]/s|^\[REAPER\]|[REAPER]\n$pair|" "$REAPER_INI"
        fi
    done
    echo "done: plugin paths set in $REAPER_INI"
fi

#### Install TAL Filter

echo "Installing TAL Filter vst..."
wget https://tal-software.com/downloads/plugins/TAL-Filter-2_64_linux.zip
unzip -o TAL-Filter-2_64_linux.zip
rm TAL-Filter-2_64_linux.zip
rm -rf /home/aaa/.local/vst/TAL-Filter-2.vst3
mkdir -p /home/aaa/.local/vst
mv -f TAL-Filter-2/TAL-Filter-2.vst3 /home/aaa/.local/vst/TAL-Filter-2.vst3
rm -rf TAL-Filter-2
echo "done: /home/aaa/.local/vst/TAL-Filter-2.vst3"

#### Install Airwindows

echo "Installing Airwindows vst..."
AIRWINDOWS_URL=$(curl -s https://api.github.com/repos/baconpaul/airwin2rack/releases/tags/DAWPlugin \
  | grep -oP '"browser_download_url":\s*"\K[^"]*Linux\.zip')
if [ -z "$AIRWINDOWS_URL" ]; then
  echo "ERROR: Could not find Airwindows download URL"
  exit 1
fi
AIRWINDOWS_FILE=$(basename "$AIRWINDOWS_URL")
echo "Downloading $AIRWINDOWS_URL"
wget "$AIRWINDOWS_URL"
unzip -o "$AIRWINDOWS_FILE"
rm "$AIRWINDOWS_FILE"
rm -rf /home/aaa/.local/clap/Airwindows\ Consolidated.clap
mkdir -p /home/aaa/.local/clap
mv -f awcons-products/Airwindows\ Consolidated.clap /home/aaa/.local/clap/airwindows.clap
rm -rf awcons-products
echo "done: /home/aaa/.local/clap/AirwindowsConsolidated.clap"

#### Install Beat Analyzer

echo "Installing Beat Analyzer..."
# Clone once, update afterwards. A plain clone fails when the directory is
# already there, and with `set -e` above that takes the whole unit down -- so
# every re-install left a3-user-install.service in `failed` and the session
# reading as degraded, for no better reason than the checkout already existing.
if [ -d /home/aaa/beat-analyzer/.git ]; then
    cd /home/aaa/beat-analyzer && git pull --ff-only --recurse-submodules
else
    cd /home/aaa && git clone --recurse-submodules https://github.com/rafjagger/beat-analyzer.git
fi

cd /home/aaa/beat-analyzer && ./build.sh

# -n, because build/.env is configuration somebody edited: ports, the clock
# mode, the JACK names. Copying the example over it on every update is the
# same fault this package's conffiles list exists to prevent. The `|| true`
# keeps a refused overwrite from failing the unit.
cp -n .env.example build/.env || true
