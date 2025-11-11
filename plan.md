# Eclipse OS Module Expansion Plan

## Overview
Plan to add three new communication protocol modules to Eclipse OS:
- **edmx** - DMX512-A lighting control protocol support
- **emidi** - MIDI musical instrument digital interface support  
- **emqtt** - MQTT messaging protocol with Home Assistant integration

## Architecture Goals
- Follow existing Eclipse OS library patterns (ecore, eio, eanim, esm structure)
- Maintain modular design with minimal dependencies
- Use smart pointers for memory management
- Implement proper tickable interfaces where appropriate
- Support RP2040/Raspberry Pi Pico hardware

---

## 1. edmx Module (DMX512-A Support)

### Purpose
Enable Eclipse OS to send and receive DMX512-A lighting control data, allowing integration with professional lighting equipment and controllers.

### Technical Reference
- **Library**: Pico-DMX by jostlowe (https://github.com/jostlowe/Pico-DMX)
  - Uses RP2040 PIO (Programmable IO) for hardware-accelerated DMX
  - Supports up to 8 parallel universes via DMA
  - Arduino-compatible API
  - BSD-3-Clause license (compatible with Eclipse OS)

### Key Features to Implement
- DMX output class (`DmxOutput`) for sending universes
- DMX input class (`DmxInput`) for receiving universes  
- Support for standard 512-channel DMX universes
- Non-blocking transmission using DMA
- Configurable pin mapping
- Universe buffer management

### File Structure
```
src/lib/edmx/
├── dmx_output.h          // DMX output interface
├── dmx_output.cpp        
├── dmx_input.h           // DMX input interface
├── dmx_input.cpp
├── dmx_universe.h        // Universe buffer management
└── dmx_universe.cpp
```

### Integration Points
- Can map HSV strip data to DMX channels for external fixtures
- Potential pattern state that outputs to DMX
- Could receive DMX as control input for patterns

### Hardware Requirements
- RS485 transceiver (MAX485 or similar)
- 3 pins minimum (TX/RX data, direction control)
- Optional: Galvanic isolation for professional use

---

## 2. emidi Module (MIDI Support)

### Purpose
Enable MIDI input/output for musical synchronization, note-based pattern triggers, and control surface integration.

### Technical Reference
- **Library**: FortySevenEffects/arduino_midi_library (https://github.com/FortySevenEffects/arduino_midi_library)
  - Mature, well-tested MIDI implementation
  - Supports MIDI 1.0 specification
  - Hardware and USB MIDI transport
  - Callback-based message handling
  - MIT license (compatible with Eclipse OS)

### Key Features to Implement
- MIDI input handler with callback system
- MIDI output for clock/sync
- Note on/off message parsing
- Control Change (CC) message support
- MIDI clock sync for tempo-based patterns
- Channel filtering

### File Structure
```
src/lib/emidi/
├── midi_interface.h      // Main MIDI interface wrapper
├── midi_interface.cpp    
├── midi_handler.h        // Message callback handlers
├── midi_handler.cpp
├── midi_clock.h          // Clock/sync utilities
└── midi_clock.cpp
```

### Integration Points
- Trigger pattern changes via MIDI notes
- Map CC values to pattern parameters
- Sync animation timing to MIDI clock
- Use velocity for brightness/intensity control
- Potential MIDI-to-DMX bridge functionality

### Hardware Requirements
- MIDI DIN input circuit (optocoupler + current limiting resistor)
- Optional: USB MIDI via Pico USB (requires Arduino-USBMIDI)
- 2 pins for serial MIDI (TX/RX)

---

## 3. emqtt Module (MQTT + Home Assistant Support)

### Purpose
Enable WiFi-based MQTT messaging for Home Assistant integration, remote control, and IoT connectivity.

### Technical Reference
- **Library**: PubSubClient by knolleary (https://github.com/knolleary/pubsubclient)
  - Lightweight MQTT 3.1.1 client
  - Publish/Subscribe support
  - QoS 0/1 support
  - 256-byte default message size (configurable)
  - MIT license (compatible with Eclipse OS)

### Key Features to Implement
- MQTT client wrapper with Eclipse OS patterns
- Home Assistant MQTT discovery support
- Pattern control via MQTT topics
- Status publishing (brightness, current pattern, etc.)
- JSON message parsing for complex commands
- Reconnection handling
- Retained message support for state persistence

### File Structure
```
src/lib/emqtt/
├── mqtt_client.h         // MQTT client wrapper
├── mqtt_client.cpp       
├── mqtt_handler.h        // Message handlers and callbacks
├── mqtt_handler.cpp
├── ha_discovery.h        // Home Assistant auto-discovery
├── ha_discovery.cpp
└── mqtt_config.h         // Configuration structures
```

### Integration Points
- Control pattern selection remotely
- Adjust brightness and parameters
- Publish status to Home Assistant
- Subscribe to automation triggers
- Sync multiple Eclipse OS devices
- Remote firmware updates (future)

### Hardware Requirements
- WiFi-capable RP2040 board (Pico W or similar)
- Network library (WiFiNINA, ESP32 WiFi, etc.)
- MQTT broker (Mosquitto, Home Assistant built-in, etc.)

### Home Assistant Integration
```yaml
# Example Home Assistant configuration
light:
  - platform: mqtt
    name: "Eclipse OS Jacket"
    state_topic: "eclipse/jacket/state"
    command_topic: "eclipse/jacket/set"
    brightness_state_topic: "eclipse/jacket/brightness"
    brightness_command_topic: "eclipse/jacket/brightness/set"
    rgb_state_topic: "eclipse/jacket/rgb"
    rgb_command_topic: "eclipse/jacket/rgb/set"
    effect_list: ["rainbow", "fire", "sparkle", "breathing"]
    effect_state_topic: "eclipse/jacket/effect"
    effect_command_topic: "eclipse/jacket/effect/set"
```

---

## Implementation Priority

### Phase 1: Core Module Setup
1. Create directory structure for all three modules
2. Add copyright headers and basic includes
3. Define base classes following Eclipse OS patterns
4. Update CMakeLists.txt if needed

### Phase 2: edmx (DMX)
- Highest priority for lighting control integration
- Most straightforward implementation (hardware-focused)
- Good foundation for understanding Eclipse OS hardware abstraction

### Phase 3: emidi (MIDI) 
- Medium priority for creative control
- Callback system can inform MQTT implementation
- Musical timing useful for pattern development

### Phase 4: emqtt (MQTT)
- Requires WiFi hardware consideration
- Most complex due to network stack
- Home Assistant integration requires careful API design
- Consider creating example patterns that respond to MQTT

---

## Testing Strategy

### edmx Testing
- Test DMX output with commercial fixtures
- Verify timing compliance with DMX512-A spec
- Test universe switching and multi-universe support
- Create example pattern that outputs to DMX

### emidi Testing  
- Test with MIDI keyboard/controller
- Verify clock sync accuracy
- Test note velocity mapping to brightness
- Create example pattern responsive to MIDI input

### emqtt Testing
- Test connection to local MQTT broker
- Verify Home Assistant discovery
- Test reconnection after network loss
- Test message throughput and latency
- Create Home Assistant automation examples

---

## Documentation Needs

For each module:
1. API documentation (method signatures, usage)
2. Hardware setup guide (pinouts, circuits)
3. Example sketches demonstrating basic usage
4. Integration examples with existing Eclipse OS patterns
5. Troubleshooting guide

---

## Considerations & Challenges

### General
- Memory usage on RP2040 (264KB RAM, 2MB Flash)
- Pin availability on different relic hardware
- Thread safety with multicore usage
- Performance impact on LED refresh rates

### edmx Specific
- RS485 voltage level conversion
- Electrical noise in DMX lines
- Universe size vs RAM constraints
- PIO resource allocation (8 state machines shared)

### emidi Specific
- MIDI timing precision requirements
- Handling MIDI running status
- SysEx message size limits
- Multiple MIDI transports (Serial, USB)

### emqtt Specific  
- WiFi module compatibility (Pico W, ESP32, etc.)
- Network latency impact on real-time control
- MQTT broker configuration requirements
- TLS/SSL support for secure connections
- Message size limits vs pattern complexity
- Home Assistant API versioning

---

## Future Enhancements

### edmx
- RDM (Remote Device Management) support
- sACN/Art-Net protocol support for networked DMX
- DMX-to-RGB fixture profile management

### emidi
- MIDI 2.0 protocol support
- MPE (MIDI Polyphonic Expression) for advanced controllers
- MIDI file playback for standalone sequences

### emqtt
- OTA (Over-The-Air) firmware updates via MQTT
- MQTT-SN for low-power operation
- State synchronization between multiple devices
- Pattern sharing via MQTT
- Web interface via MQTT + WebSocket bridge

---

## References

### DMX Resources
- DMX512-A specification: ANSI E1.11
- Pico-DMX library: https://github.com/jostlowe/Pico-DMX
- RP2040 PIO documentation: https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf

### MIDI Resources  
- MIDI 1.0 specification: https://www.midi.org/specifications
- Arduino MIDI Library: https://github.com/FortySevenEffects/arduino_midi_library
- MIDI DIN hardware: https://www.midi.org/specifications/midi-transports-specifications

### MQTT Resources
- MQTT 3.1.1 specification: https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/mqtt-v3.1.1.html
- PubSubClient: https://github.com/knolleary/pubsubclient
- Home Assistant MQTT Discovery: https://www.home-assistant.io/integrations/mqtt/
- Home Assistant Light MQTT: https://www.home-assistant.io/integrations/light.mqtt/

### RP2040 Resources
- RP2040 datasheet: https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf
- Arduino-Pico core: https://github.com/earlephilhower/arduino-pico
- Pico W WiFi: https://www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html

---

## License Compatibility

All referenced libraries are compatible with Eclipse OS (GNU GPLv3):
- Pico-DMX: BSD-3-Clause ✓
- arduino_midi_library: MIT ✓  
- PubSubClient: MIT ✓

MIT and BSD licenses are compatible with GPLv3 for integration into GPLv3 projects.

---

## Implementation Status Update (2025-11-02)

### ✅ COMPLETED: emqtt Module (MQTT + Home Assistant)

The emqtt module has been **fully implemented and verified**. Build compiles successfully.

#### Implemented Files
```
src/lib/emqtt/
├── mqtt_config.h         ✅ Configuration structures (MqttConfig, HomeAssistantConfig, etc.)
├── mqtt_client.h         ✅ MQTT client wrapper around PubSubClient
├── mqtt_client.cpp       ✅ Connection management, auto-reconnect, Tickable interface
├── mqtt_handler.h        ✅ Topic-based callback registration system
├── mqtt_handler.cpp      ✅ Message routing, PayloadParser utilities (bool, int, float, RGB)
├── ha_discovery.h        ✅ Home Assistant auto-discovery API
└── ha_discovery.cpp      ✅ JSON payload generation for lights, sensors, switches
```

#### Key Features Delivered
- **PubSubClient wrapper** with Eclipse OS Tickable interface integration
- **Automatic reconnection** with configurable intervals
- **Topic-based message routing** with lambda callback support
- **PayloadParser utility class** for parsing common data types (no exceptions, Arduino-compatible)
- **Home Assistant auto-discovery** for lights, sensors, and switches
- **Connection status tracking** (Disconnected, Connecting, Connected, Error)
- **Retained message support** for state persistence

#### Compilation Fixes Applied
1. **Removed const qualifier** from `MqttClient::isConnected()` (PubSubClient incompatibility)
2. **Replaced exception handling** with C-style string parsing (Arduino no `-fexceptions`)
   - `parseInt()`: Uses `strtol()` instead of `std::stoi()`
   - `parseFloat()`: Uses `strtof()` instead of `std::stof()`
   - `parseRGB()`: Uses custom `parseInt()` instead of `std::stoi()`
3. **All functions now Arduino-compatible** without exception overhead

#### Test Implementation: todoist_whiteboard Relic

Created a complete test relic to verify emqtt functionality:

**Location**: `/src/relics/todoist_whiteboard/`

**Hardware Configuration**:
- Single LED strip: 60 LEDs on GPIO pin 13
- Designed for whiteboard ambient lighting

**Visual Patterns Implemented**:
1. **Noise** - Perlin noise with cyberpunk palette (purple/pink)
2. **Monocolor** - Solid cyan color
3. **Rainbow** - Animated rainbow wave effect
4. **Fire** - Fire-like noise with warm palette (red/orange/yellow)

**MQTT Topics**:
```
whiteboard/pattern         - Command topic for pattern selection (noise/monocolor/rainbow/fire)
whiteboard/brightness      - Command topic for brightness (0-255)
whiteboard/power           - Command topic for power (ON/OFF)
whiteboard/state           - State topic publishing current power state
whiteboard/pattern/state   - State topic publishing current pattern
```

**Home Assistant Integration**:
- Automatically registers as a Light entity via MQTT discovery
- Pattern selection dropdown (4 effects)
- Brightness slider (0-255)
- Power toggle (ON/OFF)
- Unique ID: `whiteboard_light_01`
- Display name: "Todoist Whiteboard"

**Brightness Mapping**:
```cpp
brightness >= 192  → EBrightness::HIGH
brightness >= 64   → EBrightness::MED
brightness < 64    → EBrightness::MIN
power OFF          → EBrightness::NIGHTTRIP
```

#### Build Verification
```
✅ Compilation successful
Sketch size: 138,164 bytes (6% of program storage)
Global variables: 10,568 bytes (4% of dynamic memory)
Platform: rp2040:rp2040:rpipico
```

#### Dependencies Installed
- **PubSubClient v2.8.0** (installed via arduino-cli)

#### Code Style Compliance
All code follows Eclipse OS conventions:
- Minimal inline comments (focus on "why" not "what")
- Smart pointers for memory management (`std::unique_ptr`, `std::shared_ptr`)
- Copyright headers with Jake Rose attribution
- Tickable interface integration where appropriate
- Namespaces: `emqtt`, `todoist_whiteboard`
- CamelCase classes, snake_case functions/files

### 🔲 TODO: edmx Module (DMX512-A)
**Status**: Not started
**Priority**: Medium (after emqtt verification in production)

### 🔲 TODO: emidi Module (MIDI)
**Status**: Not started  
**Priority**: Low (creative enhancement)

---

## Next Steps for Resuming Development

### Immediate Testing (Before Production)
1. **Hardware setup required**:
   - WiFi-capable RP2040 board (Pico W recommended)
   - Configure WiFi credentials in main sketch
   - Set up MQTT broker (Mosquitto or Home Assistant built-in)
   
2. **Test todoist_whiteboard relic**:
   - Upload to Pico W with 60 LED strip on GPIO 13
   - Verify WiFi connection and MQTT broker connection
   - Confirm Home Assistant auto-discovery
   - Test pattern switching via Home Assistant UI
   - Verify brightness and power control
   - Check state publishing and retained messages

3. **Integration verification**:
   - Test reconnection after network loss
   - Verify message handling under load
   - Check memory usage during runtime
   - Monitor for any resource leaks

### emqtt Production Readiness Checklist
- [x] Core MQTT client implementation
- [x] Message routing and callbacks
- [x] Payload parsing utilities
- [x] Home Assistant discovery
- [x] Compiles without errors
- [ ] Hardware testing with real WiFi/MQTT
- [ ] Network resilience testing (disconnect/reconnect)
- [ ] Memory profiling during runtime
- [ ] Integration with other Eclipse OS relics
- [ ] Documentation for end users

### Future emqtt Enhancements (Post-Testing)
- **TLS/SSL support** for secure MQTT connections
- **OTA firmware updates** via MQTT
- **Multi-device synchronization** for coordinated patterns
- **Web interface** via MQTT + WebSocket bridge
- **More complex state management** (JSON payloads for advanced configurations)

### edmx Module Development (When Ready)
**Recommended approach based on emqtt learnings**:
1. Follow same architectural pattern (config, client, handler structure)
2. Use PIO for hardware-accelerated DMX timing
3. Implement Tickable interface for universe updates
4. Create test relic similar to todoist_whiteboard
5. Verify DMX timing compliance with oscilloscope

### emidi Module Development (When Ready)
**Recommended approach based on emqtt learnings**:
1. Callback-based message handling (similar to MQTT handler)
2. Implement Tickable for clock sync
3. Channel-based filtering system
4. Test with MIDI-responsive visual pattern
5. Consider MIDI-to-pattern parameter mapping

---

## Development Environment Notes

### Compilation
```bash
# Quick compile
./compile.sh

# Or direct arduino-cli
arduino-cli compile -b rp2040:rp2040:rpipico eclipse-os.ino

# Upload to device
./upload.sh
# Or
arduino-cli upload -p /dev/ttyACM0 --fqbn rp2040:rp2040:rpipico eclipse-os.ino
```

### Libraries Used
- **Adafruit NeoPixel v1.12.5** - LED strip control
- **PubSubClient v2.8** - MQTT client (installed for emqtt)
- **AnimatedGIF v2.2.0** - Screen rendering
- **Adafruit GC9A01A v1.1.1** - Display driver
- **Adafruit GFX v1.12.0** - Graphics primitives

### Platform
- **RP2040 Arduino Core v4.5.1** (earlephilhower/arduino-pico)
- **Board**: Raspberry Pi Pico (RP2040)
- **Clock**: 125 MHz
- **Flash**: 2 MB
- **RAM**: 264 KB

### Known Issues / Limitations
1. **Arduino lacks exception support** - All string parsing uses C-style functions
2. **PubSubClient::connected() is non-const** - Cannot use const MqttClient methods
3. **Default MQTT message limit: 256 bytes** - May need adjustment for large payloads
4. **WiFi library dependency** - Not included in base build, user must configure

---

## Resuming This Work

To pick up where we left off:

1. **Read this section first** to understand current state
2. **Test todoist_whiteboard** on real hardware with WiFi
3. **Verify emqtt stability** before marking production-ready
4. **Consider edmx or emidi** based on project needs
5. **Update this status section** with test results and any fixes

All MQTT code is complete and compiles. The primary remaining work is **hardware validation** with a real WiFi-enabled board and MQTT broker.
