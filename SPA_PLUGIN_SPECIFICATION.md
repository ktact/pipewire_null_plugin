# SPA Plugin Specification and Implementation Guide

This document provides a comprehensive guide to understanding and implementing SPA (Simple Plugin API) plugins for PipeWire. The null device plugin serves as a practical example demonstrating all key concepts.

## Table of Contents

1. [SPA Architecture Overview](#spa-architecture-overview)
2. [Plugin Structure and Lifecycle](#plugin-structure-and-lifecycle)
3. [Interface System](#interface-system)
4. [Factory Pattern](#factory-pattern)
5. [Node Implementation](#node-implementation)
6. [Real-Time Processing](#real-time-processing)
7. [Parameter System](#parameter-system)
8. [Event System](#event-system)
9. [Build Integration](#build-integration)
10. [Testing and Debugging](#testing-and-debugging)

## SPA Architecture Overview

SPA (Simple Plugin API) is PipeWire's core plugin architecture that provides:

- **Low-latency audio processing**: Real-time guarantees for audio threads
- **Object-oriented C design**: Interface-based programming without C++ overhead
- **Modular architecture**: Plugins can be loaded/unloaded dynamically
- **Type safety**: Strong typing through interface versioning
- **Extensibility**: New interfaces can be added without breaking compatibility

### Key Design Principles

1. **Zero-copy operation**: Direct buffer sharing between components
2. **Lock-free communication**: Atomic operations and wait-free algorithms
3. **Event-driven architecture**: Asynchronous operations with callbacks
4. **Format negotiation**: Automatic format conversion and compatibility
5. **Resource management**: Explicit lifecycle with RAII patterns

## Plugin Structure and Lifecycle

### Basic Plugin Structure

Every SPA plugin consists of:

```
spa/plugins/your-plugin/
├── your-plugin.c          # Main plugin entry point and factory enumeration
├── your-plugin.h          # Data structures and interface definitions
├── your-plugin-node.c     # Node implementation (source/sink/filter)
├── your-plugin-device.c   # Device management (optional)
├── your-plugin-monitor.c  # Device monitoring (optional)
└── meson.build           # Build configuration
```

### Plugin Lifecycle

1. **Discovery**: PipeWire calls `spa_handle_factory_enum()` to find factories
2. **Factory Registration**: Each factory is registered with the core
3. **Handle Creation**: Factory creates `spa_handle` objects on demand
4. **Interface Retrieval**: Clients get interfaces through `spa_handle_get_interface()`
5. **Configuration**: Parameters are set through interface methods
6. **Operation**: Node processes data or device manages hardware
7. **Cleanup**: Handles are destroyed when no longer needed

### Null Plugin Example Structure

```c
// null.c - Main entry point
SPA_EXPORT int spa_handle_factory_enum(const struct spa_handle_factory **factory, uint32_t *index)
{
    switch (*index) {
    case 0:
        *factory = &spa_null_sink_factory;  // Register null sink factory
        break;
    default:
        return 0;  // End enumeration
    }
    (*index)++;
    return 1;
}
```

## Interface System

SPA uses object-oriented programming patterns in C through interfaces.

### Interface Definition Pattern

```c
// Interface structure (vtable)
struct spa_node_methods {
    uint32_t version;
    int (*add_listener)(void *object, struct spa_hook *listener, ...);
    int (*set_param)(void *object, uint32_t id, ...);
    int (*process)(void *object);
    // ... more methods
};

// Interface implementation
struct spa_node {
    struct spa_interface iface;  // Base interface with type info
};

// Implementation object
struct my_node_state {
    struct spa_node node;        // Embedded interface (MUST be first)
    // ... implementation data
};
```

### Interface Embedding and Casting

```c
// Safe casting using spa_container_of
#define my_state_from_node(node) \
    spa_container_of((node), struct my_node_state, node)

// Usage in method implementation
static int impl_node_process(void *object) {
    struct my_node_state *state = object;  // Direct cast works due to embedding
    // ... implementation
}
```

### Type Safety

SPA ensures type safety through:

- **Interface versioning**: Each interface has a version number
- **Type identifiers**: Unique type IDs for each interface
- **Runtime checking**: spa_interface_call macros validate types

## Factory Pattern

Factories create and manage SPA handle objects.

### Factory Implementation

```c
static int impl_init(const struct spa_handle_factory *factory,
                    struct spa_handle *handle,
                    const struct spa_dict *info,
                    const struct spa_support *support,
                    uint32_t n_support)
{
    struct null_state *state = (struct null_state *) handle;

    // Extract support interfaces (log, system, loop, etc.)
    // Initialize state structure
    // Set up interface vtables

    return 0;
}

const struct spa_handle_factory spa_null_sink_factory = {
    SPA_VERSION_HANDLE_FACTORY,
    .name = "api.null.sink",
    .get_size = impl_get_size,        // Size of state structure
    .init = impl_init,                // Initialize new instance
    .enum_interface_info = impl_enum_interface_info,  // List interfaces
};
```

### Support Interface Extraction

```c
// Extract support interfaces provided by PipeWire core
for (i = 0; i < n_support; i++) {
    switch (support[i].type) {
    case SPA_TYPE_INTERFACE_Log:
        log = support[i].data;        // Logging interface
        break;
    case SPA_TYPE_INTERFACE_System:
        system = support[i].data;     // System services (time, etc.)
        break;
    case SPA_TYPE_INTERFACE_DataLoop:
        loop = support[i].data;       // Real-time processing loop
        break;
    }
}
```

## Node Implementation

Nodes are the core processing units in SPA. They can be:

- **Source nodes**: Generate audio data (microphones, file players)
- **Sink nodes**: Consume audio data (speakers, file writers, null sink)
- **Filter nodes**: Transform audio data (effects, converters)

### Node Interface Methods

#### Essential Methods

```c
static const struct spa_node_methods impl_node = {
    SPA_VERSION_NODE_METHODS,
    .add_listener = impl_node_add_listener,      // Event registration
    .set_param = impl_node_set_param,            // Configuration
    .enum_params = impl_node_enum_params,        // Capability discovery
    .send_command = impl_node_send_command,      // Lifecycle control
    .set_io = impl_node_set_io,                  // I/O area assignment
    .process = impl_node_process,                // Real-time processing
    // ... port management methods
};
```

#### Parameter Handling

Parameters configure node behavior and capabilities:

```c
static int impl_node_set_param(void *object, uint32_t id, uint32_t flags,
                              const struct spa_pod *param)
{
    struct null_state *state = object;

    switch (id) {
    case SPA_PARAM_Format:
        // Parse audio format (sample rate, channels, format)
        // Validate against node capabilities
        // Store configuration and notify listeners
        break;
    case SPA_PARAM_Buffers:
        // Configure buffer requirements
        break;
    // ... other parameter types
    }
}
```

#### Format Negotiation

```c
// Enumerate supported formats
static int impl_node_enum_params(void *object, int seq, uint32_t id, ...)
{
    switch (id) {
    case SPA_PARAM_Format:
        // Build format parameter describing capabilities
        param = spa_format_audio_raw_build(&b, SPA_PARAM_Format,
            &SPA_AUDIO_INFO_RAW_INIT(
                .format = SPA_AUDIO_FORMAT_F32P,  // Planar float
                .channels = 2,                     // Stereo
                .rate = 48000                      // 48kHz
            ));

        // Return parameter to caller
        spa_node_call(&state->hooks, struct spa_node_events, result, ...);
        break;
    }
}
```

## Real-Time Processing

The `process()` method is the heart of real-time audio processing.

### Real-Time Requirements

1. **No blocking operations**: No malloc, file I/O, system calls
2. **Deterministic timing**: Complete within audio quantum
3. **Lock-free communication**: Use atomic operations
4. **Minimal computation**: Avoid complex algorithms
5. **Graceful degradation**: Handle errors without stopping

### Processing Implementation

```c
static int impl_node_process(void *object)
{
    struct null_state *state = object;
    struct spa_io_buffers *io = state->io;

    // Check if node is ready to process
    if (!state->started || !state->have_format)
        return SPA_STATUS_OK;

    // Get buffer from I/O area
    if (io->buffer_id == SPA_ID_INVALID)
        return SPA_STATUS_OK;  // No buffer available

    struct spa_buffer *buf = io->buffers[io->buffer_id];

    // PROCESS BUFFER HERE
    // For null sink: just drop the buffer
    // For real sink: send to hardware
    // For filter: transform data

    // Mark buffer as consumed
    io->buffer_id = SPA_ID_INVALID;

    return SPA_STATUS_OK;
}
```

### Buffer Management

```c
// Buffer structure contains audio data and metadata
struct spa_buffer {
    uint32_t id;                    // Buffer identifier
    uint32_t n_metas;              // Number of metadata blocks
    uint32_t n_datas;              // Number of data blocks
    struct spa_meta *metas;        // Metadata (timestamps, etc.)
    struct spa_data *datas;        // Audio data blocks
};

// Access audio data
struct spa_data *data = &buf->datas[0];
void *audio_data = SPA_PTROFF(data->data, data->chunk->offset, void);
uint32_t size = data->chunk->size;
```

### I/O Areas

I/O areas provide low-latency communication:

```c
// Buffer I/O area for queue management
struct spa_io_buffers {
    uint32_t status;               // Buffer status
    uint32_t buffer_id;            // Current buffer ID
    struct spa_buffer **buffers;   // Buffer array
};

// Rate matching for synchronization
struct spa_io_rate_match {
    uint32_t delay;                // Processing delay
    double rate;                   // Rate adjustment
    uint64_t size;                 // Quantum size
};
```

## Parameter System

SPA uses structured parameters for configuration and negotiation.

### Parameter Types

```c
enum spa_param_type {
    SPA_PARAM_Format,              // Audio/video format
    SPA_PARAM_Buffers,            // Buffer requirements
    SPA_PARAM_IO,                 // I/O configuration
    SPA_PARAM_Props,              // Node properties
    SPA_PARAM_ProcessLatency,     // Latency information
    // ... more types
};
```

### Parameter Building

```c
// Build audio format parameter
struct spa_pod_builder b;
struct spa_pod *param;

spa_pod_builder_init(&b, buffer, sizeof(buffer));

param = spa_format_audio_raw_build(&b, SPA_PARAM_Format,
    &SPA_AUDIO_INFO_RAW_INIT(
        .format = SPA_AUDIO_FORMAT_F32P,
        .channels = 2,
        .rate = 48000
    ));
```

### Parameter Parsing

```c
// Parse received format parameter
struct spa_audio_info info;
int res = spa_format_parse(param, &info.media_type, &info.media_subtype);
if (res < 0)
    return res;

if (info.media_type == SPA_MEDIA_TYPE_audio) {
    res = spa_format_audio_raw_parse(param, &info.info.raw);
    // Use parsed format information
}
```

## Event System

SPA uses hooks for event-driven communication.

### Event Registration

```c
// Register event listener
static int impl_node_add_listener(void *object,
                                 struct spa_hook *listener,
                                 const struct spa_node_events *events,
                                 void *data)
{
    struct null_state *state = object;
    spa_hook_list_append(&state->hooks, listener, events, data);
    return 0;
}
```

### Event Emission

```c
// Emit parameter change event
spa_node_call(&state->hooks, struct spa_node_events, param_changed,
             0, id, param);

// Emit info change event
spa_node_call(&state->hooks, struct spa_node_events, info,
             &state->info);
```

### Event Handling

```c
// Event listener implementation
static const struct spa_node_events node_events = {
    SPA_VERSION_NODE_EVENTS,
    .info = handle_node_info,
    .param_changed = handle_param_changed,
};

static void handle_param_changed(void *data, int seq, uint32_t id,
                                const struct spa_pod *param)
{
    // Handle parameter change
}
```

## Build Integration

### Plugin Meson Configuration

```meson
# Plugin source files
null_sources = [
  'null.c',
  'null-sink.c',
]

# Dependencies
null_deps = [
  spa_dep,          # SPA headers and utilities
  mathlib,          # Math functions
]

# Build plugin library
null_lib = shared_library('spa-null',
  null_sources,
  include_directories : [configinc, spa_inc],
  dependencies : null_deps,
  install : true,
  install_dir : spa_plugindir / 'null',  # Install to SPA plugin directory
  install_tag : 'spa-plugins'
)
```

### Main Build System Integration

Add to `spa/plugins/meson.build`:

```meson
# Add null plugin subdirectory
if get_option('null').enabled()
  subdir('null')
endif
```

Add to `meson_options.txt`:

```meson
option('null',
  description: 'Enable null device plugin (educational example)',
  type: 'feature',
  value: 'auto'
)
```

## Testing and Debugging

### Basic Testing

```bash
# Build with null plugin enabled
meson setup build -Dnull=enabled
ninja -C build

# Test plugin loading
spa-inspect /path/to/spa-null.so

# Create null sink with PipeWire
pw-cli create-node spa-node-factory api.null.sink
```

### Runtime Configuration

```bash
# Enable debug logging for null plugin
export PIPEWIRE_DEBUG="*spa.null*:4"

# Run PipeWire with verbose logging
pipewire &

# Monitor plugin activity
journalctl -f -u pipewire
```

### Development Tips

1. **Start Simple**: Begin with basic sink that just drops buffers
2. **Use Logging**: Add comprehensive logging for debugging
3. **Test Incrementally**: Test each interface method separately
4. **Follow Patterns**: Study existing plugins for conventions
5. **Validate Parameters**: Always check input parameters
6. **Handle Errors**: Graceful degradation is essential

### Common Pitfalls

1. **Memory allocation in process()**: Never allocate in real-time context
2. **Missing parameter validation**: Always validate inputs
3. **Incorrect interface embedding**: spa_node must be first member
4. **Forgetting to increment index**: Factory enumeration requires this
5. **Thread safety issues**: Use appropriate synchronization

## Advanced Topics

### Multiple Port Nodes

For nodes with multiple ports:

```c
// Enumerate ports
static int impl_node_enum_ports(void *object, int seq,
                               enum spa_direction direction,
                               uint32_t start, uint32_t num)
{
    // Return port information for each port
}

// Port-specific parameter handling
static int impl_node_port_set_param(void *object,
                                   enum spa_direction direction,
                                   uint32_t port_id, ...)
{
    // Handle port-specific configuration
}
```

### Device and Monitor Implementation

Device factories manage hardware:

```c
// Device interface for hardware management
static const struct spa_device_methods impl_device = {
    .add_listener = impl_device_add_listener,
    .sync = impl_device_sync,
    .enum_params = impl_device_enum_params,
    .set_param = impl_device_set_param,
};

// Monitor interface for hot-plug detection
static const struct spa_device_methods impl_monitor = {
    .add_listener = impl_monitor_add_listener,
    .sync = impl_monitor_sync,
    .enum_params = impl_monitor_enum_params,
    .set_param = impl_monitor_set_param,
};
```

### Format Conversion

For format conversion nodes:

```c
// Convert between different audio formats
static int convert_audio_format(struct spa_audio_info *src_fmt,
                               struct spa_audio_info *dst_fmt,
                               void *src_data, void *dst_data,
                               uint32_t n_frames)
{
    // Implement format conversion
}
```

## Conclusion

The SPA plugin architecture provides a powerful foundation for audio processing in PipeWire. The null device plugin demonstrates all key concepts:

- **Plugin structure**: Factory enumeration and handle creation
- **Interface implementation**: Object-oriented C patterns
- **Real-time processing**: Lock-free buffer processing
- **Parameter negotiation**: Format and capability discovery
- **Event system**: Asynchronous communication

By following these patterns and principles, you can implement sophisticated audio processing plugins that integrate seamlessly with PipeWire's ecosystem while maintaining real-time performance guarantees.

The null sink example serves as a minimal but complete implementation that can be extended for more complex audio processing tasks. Use it as a starting point for your own SPA plugin development.