#!/bin/bash
echo "Installing reaper..."
wget https://www.reaper.fm/files/7.x/reaper761_linux_x86_64.tar.xz -O /tmp/reaper.tar.xz
tar -xf /tmp/reaper.tar.xz -C /tmp
cd /tmp/reaper_linux_*
./install-reaper.sh --install /opt --no-desktop --usr-local-bin-symlink