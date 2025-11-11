# Serial2MQTT Bridge for Home Assistant

Bridge serial port devices to MQTT, enabling Home Assistant integration for microcontrollers without WiFi.

## Features

- **Bidirectional communication**: Serial ↔ MQTT
- **Compatible with Eclipse OS** serial MQTT transport
- **Automatic reconnection** for both serial and MQTT
- **Lightweight**: Minimal resource usage
- **Standard protocol**: Uses serial2mqtt JSON format

## Use Cases

- Control Raspberry Pi Pico (non-WiFi) devices via MQTT
- Integrate Arduino boards without network shields
- Reliable USB connection instead of WiFi
- Lower memory footprint on microcontrollers

## Installation

### Method 1: Local Add-on (Recommended for Testing)

1. **Copy this folder to your Home Assistant**:
   ```bash
   # On your Home Assistant system
   mkdir -p /addons/serial2mqtt
   # Copy all files from hassio-addon-serial2mqtt/serial2mqtt/ to /addons/serial2mqtt/
   ```

2. **Add as local repository**:
   - Go to **Settings** → **Add-ons** → **Add-on Store**
   - Click the **⋮** menu (top right) → **Repositories**
   - Add: `/addons`
   - Refresh the page

3. **Install the add-on**:
   - Find "Serial2MQTT Bridge" in the local add-ons
   - Click **Install**

### Method 2: GitHub Repository (For Distribution)

1. Create a GitHub repository with this structure:
   ```
   your-repo/
   ├── serial2mqtt/
   │   ├── config.yaml
   │   ├── Dockerfile
   │   ├── run.py
   │   └── README.md
   └── repository.yaml
   ```

2. Add repository to Home Assistant:
   - **Settings** → **Add-ons** → **Add-on Store** → **⋮** → **Repositories**
   - Add: `https://github.com/yourusername/your-repo`

## Configuration

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `serial_port` | `/dev/ttyACM0` | USB serial port (check with `ls /dev/tty*`) |
| `baud_rate` | `9600` | Serial baud rate (must match your device) |
| `mqtt_host` | `core-mosquitto` | MQTT broker hostname |
| `mqtt_port` | `1883` | MQTT broker port |
| `mqtt_user` | `""` | MQTT username (optional) |
| `mqtt_password` | `""` | MQTT password (optional) |
| `topic_prefix` | `dst` | Prefix for device-bound topics |

### Example Configuration

```yaml
serial_port: "/dev/ttyACM0"
baud_rate: 9600
mqtt_host: "core-mosquitto"
mqtt_port: 1883
mqtt_user: ""
mqtt_password: ""
topic_prefix: "dst"
```

### Finding Your Serial Port

SSH into Home Assistant and run:
```bash
ls /dev/tty*
```

Look for devices like:
- `/dev/ttyACM0` - Common for Arduino/Pico
- `/dev/ttyUSB0` - USB serial adapters
- `/dev/serial/by-id/usb-*` - Persistent device IDs

## Protocol

This add-on uses the **serial2mqtt JSON array protocol**:

### Device → MQTT (Publish)
```json
[1, "src/device/topic", "payload", 0, 0]
```
- `1` = Publish command
- `src/device/topic` = MQTT topic
- `payload` = Message payload
- `0` = QoS level
- `0` = Retained flag (1 = retained)

### MQTT → Device (Receive)
```json
[1, "dst/device/topic", "payload"]
```

### Device Subscribe
```json
[0, "dst/device/topic"]
```

## Eclipse OS Integration

This add-on is designed to work with Eclipse OS's serial MQTT transport.

### On the Pico side (eclipse-os.ino):

```cpp
#define USE_SERIAL_MQTT 1  // Enable serial mode
```

The transport layer automatically handles the protocol conversion!

### Topic Structure

**From device (src = source):**
- `src/whiteboard/state` → Device publishes state
- `src/whiteboard/pattern/state` → Current pattern
- `src/whiteboard/brightness` → Brightness level

**To device (dst = destination):**
- `dst/whiteboard/pattern` → Change pattern
- `dst/whiteboard/brightness/set` → Set brightness
- `dst/whiteboard/power` → Power on/off

## Home Assistant Integration

Once the bridge is running, your device appears in Home Assistant via MQTT:

```yaml
# Example: Light entity
light:
  - platform: mqtt
    name: "Eclipse Whiteboard"
    state_topic: "src/whiteboard/state"
    command_topic: "dst/whiteboard/power"
    brightness_state_topic: "src/whiteboard/brightness"
    brightness_command_topic: "dst/whiteboard/brightness/set"
    effect_list: ["noise", "monocolor", "rainbow", "fire"]
    effect_state_topic: "src/whiteboard/pattern/state"
    effect_command_topic: "dst/whiteboard/pattern"
```

Or use MQTT auto-discovery (already implemented in Eclipse OS todoist_whiteboard relic)!

## Troubleshooting

### Add-on won't start

**Check logs**: Settings → Add-ons → Serial2MQTT Bridge → Logs

Common issues:
- Serial port doesn't exist: Verify with `ls /dev/tty*`
- Permission denied: Add-on has `uart: true` in config.yaml
- MQTT connection failed: Check broker hostname and credentials

### Device not responding

1. Check serial connection:
   ```bash
   # In Home Assistant terminal
   cat /dev/ttyACM0
   ```
   You should see debug output from your device.

2. Verify baud rate matches between add-on config and device

3. Check MQTT topics:
   - Settings → Devices & Services → MQTT → Configure → Listen to topic
   - Subscribe to `src/#` to see all device messages

### Debug mode

Check add-on logs for detailed serial/MQTT traffic:
- Settings → Add-ons → Serial2MQTT Bridge → Logs
- Look for `Serial → MQTT` and `MQTT → Serial` messages

## Memory Usage Comparison

**Eclipse OS on Raspberry Pi Pico:**

| Mode | Program Storage | RAM | Difference |
|------|----------------|-----|------------|
| WiFi MQTT | 440,584 bytes (21%) | 77,672 bytes (29%) | Baseline |
| Serial MQTT | 386,504 bytes (18%) | 19,280 bytes (7%) | **-54KB / -58KB** |

Serial mode saves significant resources, perfect for cheaper non-WiFi Pico boards!

## Support

For issues specific to:
- **Eclipse OS**: Check the eclipse-os repository
- **This add-on**: Open an issue in the add-on repository
- **Home Assistant**: Visit the Home Assistant community forums

## License

MIT License - See LICENSE file for details

## Credits

Inspired by:
- [vortex314/serial2mqtt](https://github.com/vortex314/serial2mqtt)
- [dgomes/serial2mqtt](https://github.com/dgomes/serial2mqtt)
- Eclipse OS project

Built for use with Eclipse OS and compatible microcontrollers.
