# APT package actions

- configure network interface in /etc/systemd/network/a3-core.network
- setup user aaa
- enable system services
  - systemd-networkd
  - x11vnc.service
  - vnc-display.service
- enable user services
  - a3-main.service
- enable lightdm autologin for user aaa
- configure audio interface in a3-jack.service
- onfigure channel routing in qjackctl make patchbay persistant .lokal/share/qjackctl
- configure irq priorities in /etc/rtirq.conf (rtirq package not in testing repo atm)
- configure core osc .lokal/bin/a3-core.py
- configure realtime privileges
- VST Plugins
  - iem-plugin-suite https://plugins.iem.at

# Manual Instructions
- install reaper
  - `.local/share/a3-core/reaper-oneshot-install.sh`
  - apply config in reaper (settings > general > import config) 
    - `.local/share/a3-core/a3_reaper_config..`
  - enter reaper license key
  - https://www.reaper.fm
- Download VST plugins
  - Airwindows plugin suite (SmoothEQ / purestGain) https://airwindows.com/vsts
  - TAL Software talfilter 2 https://tal-software.com/products/tal-filter

# Debian Repository

GPG-Key importieren: `wget -O - https://a3-audio.github.io/a3-core/KEY.gpg | sudo gpg --dearmor -o /usr/share/keyrings/a3-core-archive-keyring.gpg`

apt sources: `vim /etc/apt/sources.list.d/a3-core.sources`
```
URIs: https://a3-audio.github.io/a3-core/
Suites: ./
Components: 
Signed-By: /usr/share/keyrings/a3-core-archive-keyring.gpg
```
update & install
```
sudo apt update
sudo apt install a3-core
```

# Configure Network
`sudo dpkg-reconfigure a3-core`