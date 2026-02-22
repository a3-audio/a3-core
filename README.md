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


# add debian repository
sudo apt update
sudo apt install a3-core

## Repository einbinden (Schritt-für-Schritt)

1. GPG-Key importieren:
	wget -O - https://a3-audio.github.io/a3-core/KEY.gpg | sudo gpg --dearmor -o /usr/share/keyrings/a3-core-archive-keyring.gpg

2. Repository hinzufügen:
	echo 'deb [signed-by=/usr/share/keyrings/a3-core-archive-keyring.gpg] https://a3-audio.github.io/a3-core/ ./' | sudo tee /etc/apt/sources.list.d/a3-core.list

3. apt aktualisieren:
	sudo apt update

4. Paket installieren:
	sudo apt install a3-core


