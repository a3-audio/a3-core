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
- xonfigure channel routing in qjackctl make patchbay persistant .lokal/share/qjackctl
- configure irq priorities in /etc/rtirq.conf (rtirq package not in testing repo atm)
- configure core osc .lokal/bin/a3-core.py
- configure realtime privileges
- Download VST Plugins
  - Airwindows plugin suite (SmoothEQ / purestGain) https://airwindows.com/vsts
  - TAL Software talfilter 2
  - iem-plugin-suite

# Manual Instaructions
- download talfilter and airwindows
- enter reaper license key

**Repository einbinden**

GPG-Key importieren:
`wget -O - https://a3-audio.github.io/a3-core/KEY.gpg | sudo gpg --dearmor -o /usr/share/keyrings/a3-core-archive-keyring.gpg`

apt sources
`vim /etc/apt/sources.list.d/a3-core.sources`

Types: deb
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
