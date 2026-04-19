# Jack Graph

Jack Graph is a graphical JACK Audio Connection Kit and ALSA MIDI port connection manager. It provides an interactive visual graph interface for routing audio and MIDI signals between applications and hardware on Linux systems.

This tool is intended as an alternative to existing JACK patchbay applications like QjackCtl, Catia or Patchage, with a focus on a clean modern graph-based user interface.

---

## Features

### 🎛️ Visual Graph Interface
- Interactive canvas with drag & drop port connections
- Auto-layout algorithm for ports and clients
- Zoom in/out functionality with mouse wheel
- Pan support by dragging canvas background
- Client grouping boxes to organize ports by application

### 🎵 JACK Audio Support
- Automatic connection to running JACK server
- Live port monitoring (updates dynamically when ports appear/disappear)
- Real-time connection / disconnection of audio ports
- Display JACK server status: buffer size, sample rate, xrun count
- JACK server start/stop control directly from within the application

### 🎹 ALSA MIDI Support
- Shows ALSA hardware and software MIDI ports
- Integrated alongside JACK ports on the same canvas
- MIDI ports are visually distinguished from audio ports

### ✨ Additional Features
- Persistent window position and size configuration
- Status bar with live server information
- Manual refresh functionality for port list
- Desktop integration with system tray support
- About dialog with license and version information

---

### Installation from Tarball Release

If you downloaded a pre-compiled tarball release:

```bash
# Extract the tarball
tar -xf jack-graph.tar.gz
cd jack-graph

# Make install script executable and run it
sudo chmod +x install.sh
sudo ./install.sh
```

## Technologies

| Component           | Technology |
|---------------------|------------|
| GUI Toolkit         | GTK+ 3.0 with gtkmm-3.0 C++ bindings |
| Audio Server        | JACK Audio Connection Kit |
| MIDI System         | ALSA (Advanced Linux Sound Architecture) |
| Language            | C++17 |
| Build System        | GNU Make |
| Graphics Rendering  | Cairo 2D graphics library |

---

## Building & Installation

### Prerequisites

Install required dependencies on Debian / Ubuntu:
```bash
sudo apt install g++ make libgtkmm-3.0-dev libjack-jackd2-dev libasound2-dev pkg-config
```

### Build

```bash
# Compile the project
make

# Optional: Install system wide
sudo make install
```

### Run

```bash
# Running from source directory
./jack-graph

# If installed system wide
jack-graph
```

### Uninstall

```bash
sudo make uninstall
```

---

## Usage Instructions

1.  Start JACK server either manually or via the Settings dialog in Jack Graph
2.  All running audio applications and hardware ports will appear automatically on the canvas
3.  **To connect ports**: Click & drag from an output port (right side of client boxes) to an input port (left side)
4.  **To disconnect**: Right click on an existing connection line
5.  Use mouse wheel for zooming in and out
6.  Click and drag on empty canvas area to pan the view
7.  Drag client boxes around to rearrange layout manually

---

## Project Structure

```
jack-graph/
├── src/
│   ├── main.cpp              # Application entry point
│   ├── JackGraph.hpp/cpp     # Main GTK Window and application logic
│   ├── JackClient.hpp/cpp    # JACK audio server client implementation
│   ├── AlsaClient.hpp/cpp    # ALSA MIDI client implementation
│   ├── GraphCanvas.hpp/cpp   # Custom drawing area for node/connection graph
│   ├── Node.hpp/cpp          # Port node representation
│   ├── Connection.hpp/cpp    # Connection between two ports
│   ├── ClientBox.hpp/cpp     # Grouping container for ports from same client
│   ├── Config.hpp/cpp        # Configuration file handling
│   ├── JackServerControl.hpp/cpp # JACK server start/stop control
│   └── SettingsDialog.hpp/cpp # Settings dialog UI
├── resources/
│   └── jack-graph.desktop    # Linux desktop entry file
├── jack-graph                # Compiled executable binary
├── Makefile                  # Build configuration
├── LICENSE                   # GPL-3.0+ license
└── README.md
```

---

## License

This project is licensed under **GNU General Public License v2.0 

---

## Status

✅ Working application, version 1.0.0
- All core functionality implemented
- Stable JACK audio routing
- ALSA MIDI port listing works correctly
- GTK interface is responsive and usable
