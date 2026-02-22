# Preliminary Setup Instructions

- configure network interface
- enable system services
- setup user aaa
- enable user services
- specify x11vnc password
- enable lightdm autologin for user aaa
- configure audio interface and channel routing in qjackctl
- configure irq priorities in /etc/rtirq.conf
- configure realtime privileges (limits.conf?)
- install realtime kernel image
- get Airwindows plugin suite (we're using the SmoothEQ) https://airwindows.com/vsts 


## Repository einbinden (Schritt-für-Schritt)

1. GPG-Key importieren:
	wget -O - https://a3-audio.github.io/a3-core/KEY.gpg | sudo gpg --dearmor -o /usr/share/keyrings/a3-core-archive-keyring.gpg

2. edit /etc/apt/sources.list.d/a3-core.sources 

Types: deb
URIs: https://a3-audio.github.io/a3-core/
Suites: ./
Components: 
Signed-By: /usr/share/keyrings/a3-core-archive-keyring.gpg

3. update & install

sudo apt update
sudo apt install a3-core
