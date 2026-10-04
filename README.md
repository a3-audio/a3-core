# A³ Core

The 3D sound server of [A³ Audio](https://github.com/a3-audio/a3-system): a
Debian x86_64 machine running JACK, REAPER and the beat-analyzer. A³ Mixer and
A³ Motion control it over OSC. This repository *is* the deployment: the `.deb`
package tree under `platform-config/debian-x86_64/a3-core/`, not application
source.

**Documentation: https://a3-audio.github.io/a3-doc/**

- [Using A³ Core](https://a3-audio.github.io/a3-doc/user/a3core.html)
- [Configuration](https://a3-audio.github.io/a3-doc/configuration/core.html):
  installing, what the package does, the services, the files
- [Development](https://a3-audio.github.io/a3-doc/development/core.html):
  `a3-core.py`, the window, the OSC register

## Install

On a blank Debian with a user `aaa` who may use `sudo`
([before you start](https://a3-audio.github.io/a3-doc/configuration/core.html#core-package)):

```sh
wget -qO- "https://raw.githubusercontent.com/a3-audio/a3-core/main/platform-config/debian-x86_64/a3-core_install.sh" | sudo bash
sudo dpkg-reconfigure a3-core     # network, bridge, headless screen
sudo dpkg-reconfigure jackd2      # realtime priorities for JACK
```

The script switches the machine to Debian testing. Updates after that are
`sudo apt update && sudo apt install a3-core`.

## Build and test

A push to `main` builds the package and publishes the apt archive
(`.github/workflows/pages-deploy.yml`). The version is the last `v*` tag plus
the commits since it; `python3 tools/package_version.py` prints it. To build by
hand:

```sh
cd platform-config/debian-x86_64 && dpkg-deb --build --root-owner-group a3-core
```

Tests run on a Python with the packages from
`platform-config/debian-x86_64/a3-core/home/aaa/.local/share/a3-core/recipes/requirements.txt`
(on a Core: `~/.venv/bin/python3`):

```sh
~/.venv/bin/python3 -m unittest discover -s tools/tests
```

The OSC register test needs the other A³ repositories checked out beside this
one, as in [a3-system](https://github.com/a3-audio/a3-system); without them it
skips.

## License

REUSE-compliant: the license of each path is in `.reuse/dep5`, the texts are in
`LICENSES/` (GPL-3.0-or-later, CC-BY-SA-4.0, CC0-1.0).
