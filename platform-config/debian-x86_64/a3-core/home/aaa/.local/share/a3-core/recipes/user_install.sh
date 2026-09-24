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
if [ -f /home/aaa/.config/REAPER/reaper_config.zip ]; then
  unzip -o /home/aaa/.config/REAPER/reaper_config.zip -d /home/aaa/.config/REAPER/
  rm /home/aaa/.config/REAPER/reaper_config.zip
else
  echo "SKIP: reaper_config.zip not found"
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
