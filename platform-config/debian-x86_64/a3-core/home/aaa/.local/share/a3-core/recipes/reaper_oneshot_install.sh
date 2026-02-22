#!/bin/bash
echo "Installing reaper..."
wget https://www.reaper.fm/files/7.x/reaper712_linux_x86_64.tar.xz
tar xf reaper712_linux_x86_64.tar.xz
cd reaper_linux_x86_64
sudo ./install-reaper.sh --install /opt --no-desktop --usr-local-bin-symlink
cd ..
rm -rf reaper_linux_x86_64