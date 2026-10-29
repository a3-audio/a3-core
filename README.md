# A³ Core

The C++ program `a3-core` (Core, engine, window) is built from this repository too: see `PROGRAM.md`.

The 3D sound server of [A³ Audio](https://github.com/a3-audio/a3-system): a
Debian x86_64 machine running PipeWire, the a3-core program and the beat-analyzer. A³ Mixer and
A³ Motion control it over OSC. This repository *is* the deployment: the `.deb`
package tree under `platform-config/debian-x86_64/a3-core/`, not application
source.

**Documentation: https://a3-audio.github.io/a3-doc/**

- [Using A³ Core](https://a3-audio.github.io/a3-doc/user/a3core.html)
- [Configuration](https://a3-audio.github.io/a3-doc/configuration/core.html):
  installing, what the package does, the services, the files
- [Development](https://a3-audio.github.io/a3-doc/development/core.html):
  the Core, the window, the OSC register

## Install

On a blank Debian with a user `aaa` who may use `sudo`
([before you start](https://a3-audio.github.io/a3-doc/configuration/core.html#core-package)):

```sh
wget -qO- "https://raw.githubusercontent.com/a3-audio/a3-core/main/platform-config/debian-x86_64/a3-core_install.sh" | sudo bash
sudo dpkg-reconfigure a3-core     # network, bridge, headless screen
```

The script switches the machine to Debian testing. Updates after that are
`sudo apt update && sudo apt install a3-core`.

## Build and test

A push to `main` builds the package and publishes the apt archive
(`.github/workflows/pages-deploy.yml`). The version is the last `v*` tag plus
the commits since it; `python3 tools/package_version.py` prints it.

The repository builds two packages. `a3-core-app` is the program
(`/usr/bin/a3-core`, built from this repository's C++ source), its user units,
the engine data and its helpers; it installs on any machine, a laptop included.
Its recipe is `packaging/app/stage`, its control file and maintainer scripts are
in `packaging/app/DEBIAN/`. `a3-core` is the Core machine's deployment: the
payload under `platform-config/debian-x86_64/a3-core/`, the RT kernel and the
conflicts with what would take the machine's network over; it depends on
`a3-core-app` at its own version. Its recipe is `packaging/stage`, its control
file and maintainer scripts are in `packaging/DEBIAN/`. The packages a build
needs are listed in `packaging/build-depends`. `a3-core` takes over from the
old `a3-engine` package (`Conflicts`/`Replaces`). To build by hand, from a
clean checkout and with JUCE installed, use the umbrella's builder once per
recipe:

```sh
python3 /path/to/a3-system/installer/package.py <a3-core checkout> --recipe packaging/app --out <dir> --juce ~/local/juce
python3 /path/to/a3-system/installer/package.py <a3-core checkout> --recipe packaging --out <dir> --juce ~/local/juce
```

The package runs no Python. The tests
and the dev tools are Python on the development machine; they need only the
standard library, and read the truth through `tools/lib/a3_osc.py`:

```sh
python3 -m unittest discover -s tools/tests
```

The OSC register test needs the other A³ repositories checked out beside this
one, as in [a3-system](https://github.com/a3-audio/a3-system); without them it
skips.

## Looking at the audio connections

`a3-core --links` prints who is connected to whom in the JACK/PipeWire graph, in plain words
(`capture 1–8 → analog 1–8 (channels 1–4, L/R)`), and how many configured links have no port to land
on. Use it instead of qjackctl. Details in `PROGRAM.md`.

## Screencast for OBS

`a3-screencast` streams the picture of this machine's screen (x264 on the CPU,
MPEG-TS, no sound) as a listener on the port `screencast.stream` of
`a3-osc.json` (9930). It runs niced on core 0, which the audio units
(`CPUAffinity=1 2 3`) leave free, with no muxer delay and a short grab queue, so
the picture arrives within a frame or two. OBS takes the sound from AES67. The
package does not enable it; per machine:

```sh
systemctl --user enable --now a3-screencast
```

Optional settings in `~/.config/a3/screencast.env`: `A3_SCREENCAST_CPUS`
(default: 0), `A3_SCREENCAST_FPS` (30), `A3_SCREENCAST_CRF` (23, lower is
better).

On the viewing machine, tunnel to the rigs and add a Media Source in OBS:

```sh
ssh -N -L 5931:192.168.8.10:9930 -L 5932:192.168.8.20:9930 <jump host>
```

Uncheck *Local File*, Input `tcp://127.0.0.1:5931`, Input Format `mpegts`,
enable *Restart playback when source becomes active*, and reduce buffering.
Anyone on the rig's LAN can watch the port; it is never forwarded on the router.

## License

REUSE-compliant: the license of each path is in `.reuse/dep5`, the texts are in
`LICENSES/` (GPL-3.0-or-later, CC-BY-SA-4.0, CC0-1.0).
