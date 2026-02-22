#!/bin/bash
echo "Installing reaper..."
wget https://www.reaper.fm/files/7.x/reaper712_linux_x86_64.tar.xz
tar -xf reaper712_linux_x86_64.tar.xz
cd reaper_linux_x86_64
./install-reaper.sh --install /home/aaa/.local/bin/ --usr-local-bin-symlink --quiet
cd ..
rm -rf reaper_linux_x86_64
rm reaper712_linux_x86_64.tar.xz

echo "Installing TAL Filter vst..."
wget https://tal-software.com/downloads/plugins/TAL-Filter-2_64_linux.zip
unzip TAL-Filter-2_64_linux.zip
rm TAL-Filter-2_64_linux.zip
mv TAL-Filter-2/TAL-Filter-2.vst3 ~/.local/vst/
rm -rf TAL-Filter-2
echo "done. /home/aaa/.local/vst ..."

echo "Installing Airwindows vst..."
wget https://github.com/baconpaul/airwin2rack/releases/download/DAWPlugin/AirwindowsConsolidated-2026-02-22-b0ec35c-Linux.zip
unzip AirwindowsConsolidated-2026-02-22-b0ec35c-Linux.zip
rm AirwindowsConsolidated-2026-02-22-b0ec35c-Linux.zip
mv awcons-products/Airwindows\ Consolidated.vst3 /home/aaa/.local/vst/
rm -rf awcons-products
echo "done. /home/aaa/.local/vst ..."

