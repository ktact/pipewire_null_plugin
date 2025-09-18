# SPA Null Plugin - Educational Example

This is a complete SPA (Simple Plugin API) plugin implementation for PipeWire that demonstrates how to create audio processing plugins. The null plugin creates a sink that accepts audio buffers but drops them instead of playing them.

## Build Instructions

### Prerequisites

Make sure you have PipeWire development packages installed:

**Ubuntu/Debian:**
```bash
sudo apt install libpipewire-0.3-dev libspa-0.2-dev meson ninja-build
```

**Fedora:**
```bash
sudo dnf install pipewire-devel meson ninja-build
```

**Arch Linux:**
```bash
sudo pacman -S pipewire meson ninja
```

### Building the Plugin

1. **Configure the build:**
   ```bash
   cd null_plugin
   meson setup build
   ```

2. **Compile:**
   ```bash
   ninja -C build
   ```

3. **Install (optional):**
   ```bash
   sudo ninja -C build install
   ```

### Custom Install Directory

To install to a custom location:
```bash
meson setup build -Dspa-plugindir=/custom/path/to/spa-plugins
ninja -C build install
```

## Testing the Plugin

### 1. Verify Plugin Loading

Check if the plugin loads correctly:
```bash
spa-inspect ./build/null/spa-null.so
```

Expected output should show the null sink factory.

### 2. Manual Testing with pw-cli

Create a null sink manually:
```bash
# Start PipeWire if not running
pipewire &

# Create null sink node
pw-cli create-node spa-node-factory api.null.sink

# List nodes to verify creation
pw-cli info all | grep null
```

### 3. Connect Audio Source

Route audio to the null sink:
```bash
# Find available sources and the null sink
pw-cli ls Node

# Connect audio source to null sink (replace IDs with actual values)
pw-cli create-link <source-node-id> <output-port-id> <null-sink-id> <input-port-id>
```

### 4. Debug Logging

Enable debug logging to see plugin activity:
```bash
export PIPEWIRE_DEBUG="*spa.null*:4"
pipewire
```

## File Structure

```
null_plugin/
├── README.md                       # This file
├── SPA_PLUGIN_SPECIFICATION.md     # Complete implementation guide
├── meson.build                     # Main build configuration
├── meson_options.txt               # Build options
└── null/                           # Plugin source code
    ├── meson.build                 # Plugin build config
    ├── null.c                      # Main plugin factory
    ├── null.h                      # Data structures and interfaces
    └── null-sink.c                 # Null sink implementation
```

## What This Plugin Demonstrates

- **SPA Plugin Architecture**: Complete factory and interface implementation
- **Real-Time Processing**: Lock-free buffer processing in `impl_node_process()`
- **Format Negotiation**: Audio format parameter handling
- **Event System**: Hook-based event communication
- **Buffer Management**: I/O area communication with PipeWire graph
- **Parameter System**: Configuration through SPA parameters

## Educational Value

This plugin serves as a minimal but complete example showing:

1. **Plugin Entry Point**: `spa_handle_factory_enum()` registration
2. **Factory Pattern**: Creating spa_handle objects
3. **Interface Implementation**: spa_node vtable methods
4. **Buffer Processing**: Real-time audio processing loop
5. **Format Handling**: Audio format parsing and validation
6. **State Management**: Node lifecycle and configuration

## Next Steps

Use this plugin as a starting point for:

- **Audio Effects**: Add signal processing in `impl_node_process()`
- **File Writers**: Replace buffer dropping with file output
- **Network Sinks**: Stream audio over network protocols
- **Custom Hardware**: Interface with specialized audio hardware

See `SPA_PLUGIN_SPECIFICATION.md` for comprehensive implementation details and advanced topics.

## Troubleshooting

**Plugin not found:**
- Check installation path matches PipeWire's plugin directory
- Verify plugin loads with `spa-inspect`

**Build errors:**
- Ensure PipeWire development packages are installed
- Check meson configuration with `meson configure build`

**Runtime issues:**
- Enable debug logging: `export PIPEWIRE_DEBUG="*spa.null*:4"`
- Check PipeWire daemon logs: `journalctl -u pipewire`