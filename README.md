# Install
`wget -qO- "https://raw.githubusercontent.com/a3-audio/a3-core/main/platform-config/debian-x86_64/a3-core_install.sh" | sudo bash`

- add gpg key to apt sources.d/a3-core.sources
- apt update && apt upgrade && apt install -y a3-core >>

# apt install a3-core - postinst
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
- install iem-plugin-suite
- trigger a3-user-install.service >> 

# a3-user-install.service
this one-shot service is triggert by `apt install a3-core`
- install reaper & config
- install Airwindows plugin suite
- install TAL Software - talfilter 2
- install beat-analyzer & build

# Links:
- https://airwindows.com/vsts
- https://tal-software.com/products/tal-filter
- https://github.com/rafjagger/beat-analyzer
- https://plugins.iem.at

# Configure Network
`sudo dpkg-reconfigure a3-core`
