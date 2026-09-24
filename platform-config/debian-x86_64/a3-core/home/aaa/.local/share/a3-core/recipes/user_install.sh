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

#### Install TAL Filter

echo "Installing TAL Filter vst..."
wget https://tal-software.com/downloads/plugins/TAL-Filter-2_64_linux.zip
unzip -o TAL-Filter-2_64_linux.zip
rm TAL-Filter-2_64_linux.zip
# ~/.vst3, because that is where REAPER looks without being told. The old
# ~/.local/vst is a search path for nothing; it worked only because the
# reaper.ini we used to ship named it, and shipping that file is what
# overwrote a working configuration on 2026-09-24.
rm -rf /home/aaa/.vst3/TAL-Filter-2.vst3
mkdir -p /home/aaa/.vst3
mv -f TAL-Filter-2/TAL-Filter-2.vst3 /home/aaa/.vst3/TAL-Filter-2.vst3
rm -rf TAL-Filter-2
echo "done: /home/aaa/.vst3/TAL-Filter-2.vst3"

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
# ~/.clap, for the same reason as ~/.vst3 above.
rm -rf /home/aaa/.clap/airwindows.clap
mkdir -p /home/aaa/.clap
mv -f awcons-products/Airwindows\ Consolidated.clap /home/aaa/.clap/airwindows.clap
rm -rf awcons-products
echo "done: /home/aaa/.clap/airwindows.clap"

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
