# Jack Graph

Jack Graph is a graphical JACK Audio Connection Kit and ALSA MIDI port connection manager. It provides an interactive visual graph interface for routing audio and MIDI signals between applications and hardware on Linux systems.

Jack Graph manages the JACK server directly — you can start and stop JACK, configure your audio interface, sample rate, buffer size, and MIDI settings from within the application. No external tools or system services are required.

---

## Features

### Visual Graph Interface
- Interactive canvas with drag and drop port connections
- Auto-layout algorithm for ports and clients
- Zoom in/out with mouse wheel, pan by dragging the canvas background
- Client grouping boxes to organize ports by application

### JACK Audio Support
- Start and stop the JACK server directly from within the application
- Configure interface, sample rate, frames/period, periods/buffer, and MIDI driver
- Live port monitoring — updates dynamically when ports appear or disappear
- Real-time connection and disconnection of audio ports
- Status bar shows buffer size, sample rate, and xrun count

### ALSA MIDI Support
- Lists ALSA hardware and software MIDI ports
- Displayed alongside JACK ports on the same canvas
- MIDI ports are visually distinguished from audio ports

### Settings Dialog
- Interface dropdown shows device names (e.g. `HDA Intel PCH - ALC269VB Analog`) not raw IDs
- Realtime and synchronous mode checkboxes
- Settings are saved between sessions in `~/.config/jack-graph/config`

---

## Installation

### From a Release

Download the release tarball, extract it, and run the installer:

```bash
tar -xf jack-graph.tar.gz
cd jack-graph
sudo bash install.sh
```

The installer detects your package manager and installs the required dependencies automatically. It also adds your user to the `audio` group, which is needed for JACK realtime priority. **Log out and back in after installing** for the group change to take effect.

#### What the installer does

- Installs runtime dependencies (see table below)
- On Debian/Devuan: holds `qjackctl` with `apt-mark hold` so it is not pulled in as a recommendation when installing `jackd2` — jack-graph replaces it
- Adds your user to the `audio` group
- Installs the `jack-graph` binary to `/usr/local/bin`
- Installs the desktop entry to `/usr/share/applications`

#### Runtime dependencies by distro

| Distro | Packages installed |
|--------|--------------------|
| Debian / Devuan / Ubuntu | `jackd2` `libgtkmm-3.0-1v5` `libasound2` `alsa-utils` |
| Arch / Artix | `jack2` `gtkmm3` `alsa-lib` `alsa-utils` |
| Void | `jack` `gtkmm3` `alsa-lib` `alsa-utils` |
| Fedora | `jack-audio-connection-kit` `gtkmm30` `alsa-lib` `alsa-utils` |

If your package manager is not detected, the installer will print the packages you need to install manually.

### Uninstall

```bash
sudo bash uninstall.sh
```

This removes the binary and desktop entry, and removes the `apt-mark hold` on `qjackctl` if it was set. Runtime dependencies are left in place.

---

## Building from Source

### Build dependencies

**Debian / Devuan / Ubuntu:**
```bash
sudo apt install g++ make pkg-config libgtkmm-3.0-dev libjack-jackd2-dev libasound2-dev
```

**Arch / Artix:**
```bash
sudo pacman -S base-devel jack2 gtkmm3 alsa-lib
```

**Void:**
```bash
sudo xbps-install gcc make pkg-config gtkmm3-devel jack-devel alsa-lib-devel
```

**Fedora:**
```bash
sudo dnf install gcc-c++ make pkgconf gtkmm30-devel jack-audio-connection-kit-devel alsa-lib-devel
```

### Build and install

```bash
make
sudo bash install.sh
```

---

## Usage

1. Open Jack Graph from your application menu or run `jack-graph` in a terminal
2. Go to **Settings → JACK Settings**
3. Select your audio interface from the dropdown (shows device names, not raw IDs)
4. Set your desired sample rate, frames/period, and periods/buffer
5. Press **Start** to launch the JACK server
6. All running audio applications and hardware ports appear automatically on the canvas
7. **Connect ports**: click and drag from an output (right side of a client box) to an input (left side)
8. **Disconnect**: right-click an existing connection line
9. Press **Stop** in the Settings dialog to shut down the JACK server
10. Use the mouse wheel to zoom, drag empty canvas to pan

---

## Technologies

| Component | Technology |
|-----------|------------|
| GUI Toolkit | GTK+ 3.0 with gtkmm-3.0 C++ bindings |
| Audio Server | JACK Audio Connection Kit (jackd2) |
| MIDI System | ALSA (Advanced Linux Sound Architecture) |
| Language | C++17 |
| Build System | GNU Make |
| Graphics | Cairo 2D graphics library |

---

## Project Structure

```
jack-graph/
├── src/
│   ├── main.cpp                      # Application entry point
│   ├── JackGraph.hpp/cpp             # Main window and application logic
│   ├── JackClient.hpp/cpp            # JACK audio server client
│   ├── AlsaClient.hpp/cpp            # ALSA MIDI client
│   ├── GraphCanvas.hpp/cpp           # Canvas for node/connection graph
│   ├── Node.hpp/cpp                  # Port node representation
│   ├── Connection.hpp/cpp            # Connection between two ports
│   ├── ClientBox.hpp/cpp             # Grouping container for client ports
│   ├── Config.hpp/cpp                # Configuration file (~/.config/jack-graph/config)
│   ├── JackServerControl.hpp/cpp     # JACK server start/stop (fork/exec jackd directly)
│   └── SettingsDialog.hpp/cpp        # Settings dialog UI
├── resources/
│   └── jack-graph.desktop            # Desktop entry file
├── install.sh                        # Installer (multi-distro)
├── uninstall.sh                      # Uninstaller
├── Makefile                          # Build configuration
└── LICENSE                           # GPL-2.0
```

---

## License

This project is licensed under the **GNU General Public License v2.0**. See [LICENSE](LICENSE) for details.
