#!/bin/bash
echo "Installing reaper..."
wget https://www.reaper.fm/files/7.x/reaper712_linux_x86_64.tar.xz
tar -xf reaper712_linux_x86_64.tar.xz
cd reaper_linux_x86_64
./install-reaper.sh --install /opt --usr-local-bin-symlink --quiet
cd ..
rm -rf reaper_linux_x86_64
rm reaper712_linux_x86_64.tar.xz
