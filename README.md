# A³ Core

The 3D sound server: the machine that carries the audio. A Debian x86_64
installation running JACK, REAPER and SuperCollider, remote-controlled over
OSC by [A³ Mixer](https://github.com/a3-audio/a3-mixer) and
[A³ Motion](https://github.com/a3-audio/a3-motion).

This repository *is* the deployment: the `.deb` package tree under
`platform-config/`, not application source.

## Prerequisites
- Blank debian installation
  - user: aaa
  - without desktop environment
  - with ssh server

## Install
- login to your debian
  - `su root`
  - `apt install sudo wget`
  - `/usr/sbin/usermod -aG sudo aaa`
- logout and back in
  - `wget -qO- "https://raw.githubusercontent.com/a3-audio/a3-core/main/platform-config/debian-x86_64/a3-core_install.sh" | sudo bash`

## Config
- enable realtime priorities `sudo dpkg-reconfigure jackd2`
- configure network `sudo dpkg-reconfigure a3-core`

## apt install a3-core - postinst
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

## a3-user-install.service
this one-shot service is triggert by `apt install a3-core`
- install reaper & config
- install Airwindows plugin suite
- install TAL Software - talfilter 2
- install beat-analyzer & build

## Links:
- https://airwindows.com/vsts
- https://tal-software.com/products/tal-filter
- https://github.com/rafjagger/beat-analyzer
- https://plugins.iem.at

## Where this fits

A³ is seven repositories and one system. **The structure, the workflow and the
versioning are described once, in the umbrella:**
[a3-audio/a3-system](https://github.com/a3-audio/a3-system#repositories-and-versioning).

The short of it: work happens on `main`, a version is an annotated tag, and
the same tag name is set in every repository at once — `v03.0` is the first.
