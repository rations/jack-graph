# Jack Graph

Jack Graph is a graphical connection manager for the JACK Audio Connection Kit and ALSA MIDI. It
shows the audio and MIDI ports of running applications and hardware as an interactive graph, and
you connect and disconnect them by dragging cables.

Jack Graph also manages the JACK server itself. You can start and stop JACK, and choose your audio
interface, sample rate, buffer size and MIDI driver, from inside the application. No external tools
or system services are required.

Version 2 draws its own interface with X11, Cairo and FreeType. There is **no GTK, GLib or Pango**,
and no widget toolkit of any kind.

---

## Features

### Visual graph
- Drag from an output port (right side of a box) to an input port (left side) to connect.
- Right-click a cable to disconnect it.
- Drag a box to move it; drag empty canvas to pan; the mouse wheel zooms about the pointer.
- Stereo pairs (`out` / `outR`) are kept next to each other.
- Boxes stay where you put them when JACK clients come and go. **Refresh** lays the graph out
  again from scratch.
- Toolbar: Refresh, Zoom −, Zoom +, Zoom 1:1, Fit, JACK Settings, About.

### JACK
- Start and stop the JACK server from **JACK Settings**. Starting and stopping never freeze the
  window.
- If jackd fails to start (for example, the device is busy or doesn't support the sample rate),
  jackd's own explanation is shown in the settings window.
- **Apply Live** changes frames/period on the running server without a restart.
- The graph updates live as ports appear and disappear. If the server stops or crashes,
  Jack Graph notices and reconnects when it comes back.
- The status bar shows buffer size, sample rate and xrun count.
- jackd keeps running after you close Jack Graph. The next time you open it, Jack Graph picks the
  server up again and can still stop it.

### ALSA MIDI
- While JACK is not running, the ALSA sequencer's MIDI ports are shown instead. You can connect and
  disconnect them the same way; this is what `aconnect` does.
- The view updates live when a MIDI device is plugged in or a connection is made elsewhere.

### Settings
- The Interface dropdown shows device names (for example `HDA Intel PCH - ALC897 Analog`) and
  stores stable ALSA ids (`hw:CARD=PCH`), so the choice survives plugging in a USB interface.
- Options: Realtime, and 16-bit samples (jackd's ALSA `-S`, "shorts").
- MIDI driver: None, ALSA sequencer (`-X seq`) or ALSA raw MIDI (`-X raw`).
- Settings and the window size are saved in `~/.config/jack-graph/config`.

---

## Installation

### From a release tarball

Download the tarball for your machine. Use `jack-graph-<version>-linux-x86_64.tar.gz` for a PC, or
`jack-graph-<version>-linux-aarch64.tar.gz` for a Raspberry Pi or another 64-bit ARM board. Then
extract it and run the installer:

```bash
tar -xf jack-graph-<version>-linux-<arch>.tar.gz
cd jack-graph
sudo ./install.sh
```

The installer:

- Installs the runtime dependencies with your package manager (see the table below).
- On Debian, Devuan and Ubuntu, holds `qjackctl` with `apt-mark hold`, so it isn't pulled in as a
  recommendation of `jackd2`. Jack Graph replaces it.
- Adds your user to the `audio` group, which JACK needs for realtime priority. **Log out and back
  in after installing** so the group change takes effect.
- Installs `jack-graph` to `/usr/local/bin`, its fonts to `/usr/local/share/jack-graph/fonts`, and
  the desktop entry to `/usr/share/applications`.

The tarball contains a prebuilt binary **and the full source**. If the prebuilt binary is for a
different architecture, or can't find its libraries on your system, the installer builds from the
bundled source instead. Pass `--from-source` to always build from source, and `--no-deps` to skip
installing packages.

#### Runtime dependencies

| Distro | Packages |
|--------|----------|
| Debian / Devuan / Ubuntu | `jackd2` `libjack-jackd2-0` `libasound2` `libcairo2` `libfreetype6` `libx11-6` |
| Arch / Artix | `jack2` `alsa-lib` `cairo` `freetype2` `libx11` |
| Void | `jack` `alsa-lib` `cairo` `freetype` `libX11` |
| Fedora | `jack-audio-connection-kit` `alsa-lib` `cairo` `freetype` `libX11` |

### From a .deb (Debian 12 and later, Devuan, Raspberry Pi OS)

```bash
sudo apt install --no-install-recommends ./jack-graph_<version>_<arch>.deb
```

Use `--no-install-recommends`, because `jackd2` otherwise pulls in `qjackctl`. Unlike `install.sh`,
the package doesn't add you to the `audio` group. To do that yourself:

```bash
sudo usermod -aG audio "$USER"
```

### Uninstall

```bash
sudo ./uninstall.sh
```

This removes the binary, the fonts and the desktop entry, and releases the `apt-mark hold` on
`qjackctl` if it was set. Your settings and the runtime dependencies are left in place.

---

## Usage

1. Open Jack Graph from your application menu, or run `jack-graph` in a terminal.
2. Press **JACK Settings**.
3. Choose your audio interface, sample rate, frames/period and periods/buffer.
4. Press **Start**. The graph fills in as soon as the server is up.
5. **Connect ports**: drag from an output on the right of a box to an input on the left of
   another.
6. **Disconnect**: right-click a cable.
7. To change the buffer size while JACK is running, pick a new Frames/Period and press **Apply
   Live**. Everything else needs **Stop** then **Start**.

Command line:

```
jack-graph [--scale N] [--version]
  --scale N   draw at N times the normal size (HiDPI); also read from $JACK_GRAPH_SCALE
```

jackd's output goes to `$XDG_RUNTIME_DIR/jack-graph/jackd.log`, or to `~/.cache/jack-graph/` when
`XDG_RUNTIME_DIR` isn't set.

---

## Building from source

### Build dependencies

**Debian / Devuan / Ubuntu:**
```bash
sudo apt install g++ make pkg-config libjack-jackd2-dev libasound2-dev libcairo2-dev libfreetype-dev libx11-dev
```

**Arch / Artix:**
```bash
sudo pacman -S base-devel jack2 alsa-lib cairo freetype2 libx11
```

**Void:**
```bash
sudo xbps-install gcc make pkg-config jack-devel alsa-lib-devel cairo-devel freetype-devel libX11-devel
```

**Fedora:**
```bash
sudo dnf install gcc-c++ make pkgconf jack-audio-connection-kit-devel alsa-lib-devel cairo-devel freetype-devel libX11-devel
```

### Build and run

```bash
make
./jack-graph          # runs from the build tree; finds its fonts in ./resources
sudo make install     # or: PREFIX=/usr, DESTDIR=... for packaging
```

`make` also builds `tools/uirender`, a headless layout check. It draws every window with the
bundled fonts and fails if any label overflows its slot or uses a character the fonts lack. It
needs no X server and no sound card:

```bash
./tools/uirender --out ui
```

---

## Releases

The version lives in [`VERSION`](VERSION). The Makefile, `--version`, the About box, the tarball
name and the .deb all read it. Bump it when cutting a release.

- **x86_64** is built by hand on a Debian 12 machine, to keep the glibc floor at 2.36:
  ```bash
  ./release-tarball.sh          # jack-graph-<version>-linux-x86_64.tar.gz
  packaging/build-deb.sh        # jack-graph_<version>_amd64.deb
  ```
- **aarch64** is built by the [`Linux aarch64`](.github/workflows/linux-aarch64.yml) GitHub Actions
  workflow, on every push, in a `debian:12` container on an arm64 runner. Before uploading anything
  it checks that:
  - the build is warning-free;
  - the binary is an aarch64 ELF;
  - it needs no glibc symbol newer than 2.36;
  - it passes the dependency allowlist (`scripts/check-deps.sh`);
  - it passes the `uirender` audit;
  - the bundled source builds on its own;
  - the tarball and .deb names match `VERSION`.

  The tarball, the arm64 .deb and the audit's images are uploaded as run artifacts, and attached
  to a release by hand once tested on a board.

---

## Technologies

| Component | Technology |
|-----------|------------|
| Window and input | Xlib |
| Drawing | Cairo |
| Text | FreeType, with bundled Roboto and Michroma fonts |
| Audio server | JACK (jackd2) |
| MIDI | ALSA sequencer |
| Language | C++17 |
| Build system | GNU Make |

---

## Project structure

```
jack-graph/
├── src/
│   ├── main.cpp                   # Entry point: the windows and the event loop (the only X11 user here)
│   ├── app.h/cpp                  # The application: JACK, ALSA, config, server start/stop, panels
│   ├── graphpanel.h/cpp           # The graph canvas: layout, pan, zoom, drag to connect
│   ├── settingspanel.h/cpp        # The JACK Settings window
│   ├── chrome.h/cpp               # Toolbar, status line, About card
│   ├── graphgeometry.h            # Every size and position in the windows
│   ├── JackClient.hpp/cpp         # JACK client: ports, connections, callbacks
│   ├── JackServerControl.hpp/cpp  # Starts and stops jackd (fork/exec), lists ALSA devices
│   ├── AlsaClient.hpp/cpp         # ALSA sequencer: MIDI ports, subscriptions, announcements
│   ├── Config.hpp/cpp             # ~/.config/jack-graph/config
│   ├── Node / Connection / ClientBox  # The graph's data model
│   ├── gfx/                       # Cairo + FreeType drawing and widgets (no X11)
│   └── platform/                  # X11 window, event loop, self-pipe, paths
├── tools/uirender.cpp             # Headless layout audit
├── resources/                     # Bundled fonts (with licences) and the desktop entry
├── packaging/                     # install.sh, uninstall.sh, build-deb.sh
├── scripts/check-deps.sh          # Linked-library allowlist
├── release-tarball.sh             # Builds the release tarball
├── VERSION
└── LICENSE                        # GPL-2.0
```

---

## License

This project is licensed under the **GNU General Public License v2.0**. See [LICENSE](LICENSE) for
details. The bundled fonts are under their own licences: Roboto (Apache-2.0) and Michroma (OFL-1.1),
included in `resources/fonts/`.
