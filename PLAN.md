# Jack-Graph Implementation Plan

## Overview
Create **jack-graph**: A modern JACK/ALSA port connection manager using GTK3/C++ with drag-to-connect interface, replacing the obsolete patchage application.

## Target Platform
- **Distribution**: Debian/Devuan Trixie (13)
- **Architecture**: amd64 (primary), portable to others
- **License**: GPL-3.0+ (matching patchage's license)

## Dependencies (All Available in Trixie)
| Package | Purpose | Version |
|---------|---------|---------|
| `libgtkmm-3.0-dev` | C++ GTK3 bindings | 3.24.10 |
| `libgtk-3-dev` | GTK3 core | 3.24.49 |
| `libjack-jackd2-dev` | JACK2 audio API | 1.9.22 |
| `libasound2-dev` | ALSA MIDI support | Latest |
| `libcairomm-1.0-dev` | C++ Cairo bindings (for custom drawing) | Included |
| `libglibmm-2.4-dev` | C++ GLib bindings | Included |
| `pkg-config` | Build dependency resolution | Latest |

## Architecture

### File Structure
```
jack-graph/
├── Makefile                    # Simple build system
├── src/
│   ├── main.cpp               # Application entry point
│   ├── JackGraph.cpp          # Main application class
│   ├── JackGraph.hpp          # Main application header
│   ├── JackClient.cpp         # JACK client wrapper
│   ├── JackClient.hpp         # JACK client header
│   ├── AlsaClient.cpp         # ALSA sequencer wrapper
│   ├── AlsaClient.hpp         # ALSA client header
│   ├── GraphCanvas.cpp        # GTK3 DrawingArea with Cairo rendering
│   ├── GraphCanvas.hpp        # Canvas header
│   ├── Node.cpp               # Port node representation
│   ├── Node.hpp               # Node header
│   ├── Connection.cpp         # Connection between nodes
│   ├── Connection.hpp         # Connection header
│   └── Config.cpp             # Configuration management
├── resources/
│   ├── jack-graph.desktop     # Desktop entry file
│   └── icons/                 # Application icons
└── debian/                    # Debian packaging (optional)
    ├── control
    ├── rules
    ├── changelog
    └── source/format
```

### Component Design

#### 1. Main Application (JackGraph)
- **Responsibilities**:
  - Initialize GTK3 application
  - Create main window with menu bar and canvas
  - Manage JACK and ALSA client connections
  - Handle application lifecycle
  - Provide settings dialog for frame/buffer rate

- **Key Members**:
  - `Gtk::Window` - Main application window
  - `Gtk::MenuBar` - Application menus
  - `GraphCanvas` - Custom drawing widget
  - `JackClient` - JACK audio/MIDI manager
  - `AlsaClient` - ALSA MIDI manager
  - `Config` - User preferences

#### 2. JACK Client (JackClient)
- **Responsibilities**:
  - Connect to JACK server via `jack_client_open()`
  - Monitor port registration/deregistration via callbacks
  - Monitor connection changes via callbacks
  - Provide port enumeration via `jack_get_ports()`
  - Create/remove connections via `jack_connect()`/`jack_disconnect()`
  - Report buffer size and sample rate

- **Key JACK APIs**:
  - `jack_client_open()` - Connect to server
  - `jack_on_port_register_callback()` - Port change notifications
  - `jack_on_port_disconnect_callback()` - Connection change notifications
  - `jack_get_ports()` - List all ports
  - `jack_port_name()`, `jack_port_type()`, `jack_port_flags()` - Port details
  - `jack_connect()`, `jack_disconnect()` - Manage connections
  - `jack_get_buffer_size()`, `jack_get_sample_rate()` - Server config

- **Port Types to Support**:
  - `JACK_DEFAULT_AUDIO_TYPE` - Audio ports
  - `"MIDI"` - MIDI ports
  - Input vs Output (determined by `JackPortIsInput`/`JackPortIsOutput` flags)

#### 3. ALSA Client (AlsaClient)
- **Responsibilities**:
  - Connect to ALSA sequencer via `snd_seq_open()`
  - Enumerate MIDI ports via `snd_seq_query_port_info()`
  - Monitor port changes via ALSA event queue
  - Create/remove connections via `snd_seq_connect_to()`/`snd_seq_disconnect_to()`
  - Provide human-readable port names

- **Key ALSA APIs**:
  - `snd_seq_open()` - Open sequencer
  - `snd_seq_create_simple_port()` - Create client port
  - `snd_seq_query_subscribe()` - Query existing connections
  - `snd_seq_connect_to()`, `snd_seq_disconnect_to()` - Manage connections
  - `snd_seq_query_port_info()` - Get port details
  - `snd_seq_event_input()` - Read events for monitoring

#### 4. Graph Canvas (GraphCanvas)
- **Responsibilities**:
  - Custom `Gtk::DrawingArea` widget
  - Render nodes (ports) as colored rectangles with labels
  - Render connections as curved lines (Bezier curves)
  - Handle mouse events for drag-to-connect
  - Support zoom and pan
  - Auto-layout using simple grid or force-directed algorithm

- **Rendering Approach**:
  - Use Cairo for all custom drawing
  - Override `on_draw()` method with Cairo context
  - Draw nodes as rounded rectangles with port indicators
  - Draw connections as cubic Bezier curves
  - Color-code by port type (audio vs MIDI, input vs output)

- **Mouse Interaction**:
  - `button_press_event` - Start drag from output port
  - `motion_notify_event` - Show preview line while dragging
  - `button_release_event` - Complete connection if dropped on input port
  - Right-click on connection to disconnect

- **Visual Design**:
  - Left side: Output ports (sources)
  - Right side: Input ports (destinations)
  - Audio ports: Blue tones
  - MIDI ports: Green tones
  - Connections: Match source port color
  - Selected/highlighted: Brighter colors

#### 5. Node (Node)
- **Represents**: A single JACK or ALSA port
- **Properties**:
  - `name` - Port name (e.g., "system:playback_1")
  - `type` - Audio or MIDI
  - `direction` - Input or Output
  - `client_name` - Parent application name
  - `position` - X,Y coordinates on canvas
  - `connections` - List of connected nodes

#### 6. Connection (Connection)
- **Represents**: A link between two ports
- **Properties**:
  - `source` - Output port node
  - `destination` - Input port node
  - `type` - Audio or MIDI

#### 7. Configuration (Config)
- **Responsibilities**:
  - Store user preferences in `~/.config/jack-graph/`
  - Manage settings: buffer size, sample rate preferences
  - Save/restore window geometry
  - Remember zoom level and layout preferences

## Implementation Steps

### Phase 1: Project Setup and Basic UI
1. Create project structure
2. Write Makefile with pkg-config integration
3. Create main.cpp with basic GTK3 window
4. Add menu bar with File, View, Help menus
5. Add basic DrawingArea placeholder

### Phase 2: JACK Integration
1. Implement JackClient class
2. Connect to JACK server
3. Enumerate and display ports as nodes
4. Handle port registration callbacks
5. Implement connection management

### Phase 3: Canvas Rendering
1. Implement GraphCanvas with Cairo drawing
2. Draw nodes as colored rectangles
3. Draw connections as Bezier curves
4. Implement auto-layout algorithm
5. Add zoom and pan support

### Phase 4: Drag-to-Connect
1. Implement mouse event handling
2. Add drag preview line
3. Implement connection creation on drop
4. Add right-click disconnect
5. Visual feedback during drag operations

### Phase 5: ALSA Integration
1. Implement AlsaClient class
2. Enumerate ALSA MIDI ports
3. Add ALSA nodes to canvas (different color/style)
4. Implement ALSA connection management
5. Handle ALSA port change events

### Phase 6: Settings and Polish
1. Add settings dialog for buffer/sample rate
2. Implement configuration save/load
3. Add status bar with JACK server info
4. Add error handling and user feedback
5. Create desktop entry and icons

## Key Technical Details

### Port Detection Strategy
```cpp
// JACK: Get all ports
const char** ports = jack_get_ports(client, nullptr, nullptr, 0);
for (int i = 0; ports[i]; ++i) {
    jack_port_t* port = jack_port_by_name(client, ports[i]);
    int flags = jack_port_flags(port);
    bool is_input = flags & JackPortIsInput;
    bool is_output = flags & JackPortIsOutput;
    const char* type = jack_port_type(port);
    // Create node...
}
jack_free(ports);
```

### Connection Creation
```cpp
// JACK: Connect two ports
int result = jack_connect(client, source_port_name, dest_port_name);
if (result == 0) {
    // Success
} else if (result == EEXIST) {
    // Already connected
}
```

### Canvas Drawing
```cpp
bool GraphCanvas::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
    // Clear background
    cr->set_source_rgb(0.95, 0.95, 0.95);
    cr->paint();
    
    // Draw connections first (behind nodes)
    for (auto& conn : connections) {
        draw_connection(cr, conn);
    }
    
    // Draw nodes on top
    for (auto& node : nodes) {
        draw_node(cr, node);
    }
    
    // Draw drag preview if active
    if (is_dragging) {
        draw_drag_preview(cr);
    }
    
    return true;
}
```

### Drag-to-Connect Logic
```cpp
bool GraphCanvas::on_button_press_event(GdkEventButton* event) {
    if (event->button == 1) { // Left click
        Node* node = find_node_at(event->x, event->y);
        if (node && node->is_output) {
            drag_source = node;
            drag_start_x = event->x;
            drag_start_y = event->y;
            is_dragging = true;
            return true;
        }
    }
    return false;
}

bool GraphCanvas::on_button_release_event(GdkEventButton* event) {
    if (is_dragging && drag_source) {
        Node* target = find_node_at(event->x, event->y);
        if (target && target->is_input) {
            create_connection(drag_source, target);
        }
        is_dragging = false;
        drag_source = nullptr;
        queue_draw();
        return true;
    }
    return false;
}
```

### Auto-Layout Algorithm
- Group ports by client application
- Arrange clients in columns (left=outputs, right=inputs)
- Space ports evenly within each client group
- Adjust canvas size to fit all nodes
- Simple grid layout (can be enhanced with force-directed later)

## Build System (Makefile)
```makefile
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra $(shell pkg-config --cflags gtkmm-3.0 jack alsa)
LDFLAGS = $(shell pkg-config --libs gtkmm-3.0 jack alsa)

SOURCES = src/main.cpp src/JackGraph.cpp src/JackClient.cpp \
          src/AlsaClient.cpp src/GraphCanvas.cpp src/Node.cpp \
          src/Connection.cpp src/Config.cpp
OBJECTS = $(SOURCES:.cpp=.o)
TARGET = jack-graph

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)/usr/bin
	install $(TARGET) $(DESTDIR)/usr/bin
	install -d $(DESTDIR)/usr/share/applications
	install resources/jack-graph.desktop $(DESTDIR)/usr/share/applications/

.PHONY: clean install
```

## Debian Packaging Notes
- Package name: `jack-graph`
- Section: `sound`
- Priority: `optional`
- Build-Depends: `debhelper, libgtkmm-3.0-dev, libjack-jackd2-dev, libasound2-dev`
- Depends: `${shlibs:Depends}, ${misc:Depends}`
- Architecture: `any`

## Testing Strategy
1. **Unit Tests**: Test JackClient and AlsaClient port enumeration
2. **Integration Tests**: Test connection creation/disconnection
3. **Manual Testing**: 
   - Start JACK server with `jackd`
   - Launch multiple JACK clients (e.g., `ardour`, `qsynth`, `carla`)
   - Verify ports appear in jack-graph
   - Test drag-to-connect functionality
   - Test ALSA MIDI port detection
   - Verify buffer size/sample rate display

## Risk Mitigation
| Risk | Mitigation |
|------|------------|
| JACK server not running | Graceful error message, offer to retry |
| No ALSA MIDI ports | Hide ALSA section or show "No MIDI ports" |
| Complex graphs with many ports | Implement scrolling/zooming, lazy rendering |
| GTK3 deprecation | Code is clean enough to port to GTK4 later |
| Threading issues | Use `Glib::signal_idle()` for UI updates from JACK callbacks |

## Success Criteria
- [x] Compiles on Debian Trixie with available packages
- [x] Detects JACK ports automatically
- [x] Detects ALSA MIDI ports automatically
- [x] Drag-to-connect works for both JACK and ALSA
- [x] Displays buffer size and sample rate
- [x] Clean, modern GTK3 interface
- [x] Debian package builds successfully

## Future Enhancements (Post-v1)
- Force-directed graph layout
- Save/load connection presets
- Port grouping and filtering
- Dark theme support
- GTK4 migration
- Real-time DSP load monitoring
- Connection latency display
