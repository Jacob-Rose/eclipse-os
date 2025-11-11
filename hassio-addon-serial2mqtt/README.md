# Serial2MQTT Home Assistant Add-on

**Bridge your Eclipse OS devices (and other microcontrollers) to Home Assistant via USB serial instead of WiFi!**

## Why Use This?

- ✅ **Works with non-WiFi Pico boards** - Use cheaper RP2040 without WiFi
- ✅ **Better signal reliability** - USB cable instead of WiFi
- ✅ **Lower memory usage** - Saves 54KB program + 58KB RAM on device
- ✅ **Drop-in replacement** - Eclipse OS supports both WiFi and Serial MQTT
- ✅ **Fully compatible** - Same MQTT topics, Home Assistant auto-discovery works

## Quick Start

### 1. Install the Add-on

**Easiest method (local installation):**

1. Copy the `serial2mqtt/` folder to `/addons/serial2mqtt/` on your Home Assistant
2. Settings → Add-ons → ⋮ → Repositories → Add `/addons`
3. Install "Serial2MQTT Bridge" from Local add-ons
4. Configure and start

**Detailed instructions:** See [INSTALLATION.md](INSTALLATION.md)

### 2. Configure Your Eclipse OS Device

In `eclipse-os.ino`:
```cpp
#define USE_SERIAL_MQTT 1  // Enable USB serial mode
```

Compile and upload to your Pico.

### 3. Connect and Enjoy

Plug your Pico into Home Assistant via USB. It will automatically appear as an MQTT device!

## Features

- **Bidirectional communication**: Full control and status updates
- **Auto-reconnection**: Handles serial port and MQTT disconnections
- **Multi-architecture**: Works on all Home Assistant platforms
- **Lightweight**: Minimal resource usage (~50 lines of Python)
- **Standard protocol**: Uses serial2mqtt JSON format
- **Debug logging**: Easy troubleshooting

## What's Included

```
hassio-addon-serial2mqtt/
├── serial2mqtt/              # The actual add-on
│   ├── config.yaml          # Add-on metadata & options
│   ├── Dockerfile           # Container build
│   ├── build.yaml           # Multi-arch config
│   ├── run.py               # Bridge script
│   ├── README.md            # Full documentation
│   └── CHANGELOG.md         # Version history
├── repository.yaml          # For GitHub distribution
├── INSTALLATION.md          # Step-by-step install guide
└── README.md                # This file
```

## Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `serial_port` | `/dev/ttyACM0` | USB serial port |
| `baud_rate` | `9600` | Must match device |
| `mqtt_host` | `core-mosquitto` | MQTT broker |
| `mqtt_port` | `1883` | MQTT port |
| `mqtt_user` | `""` | Optional auth |
| `mqtt_password` | `""` | Optional auth |
| `topic_prefix` | `dst` | Device-bound topic prefix |

## How It Works

```
┌─────────────────┐         ┌──────────────────┐         ┌─────────────┐
│  Eclipse OS     │  USB    │  Serial2MQTT     │  MQTT   │    Home     │
│  (Pico)         ├────────►│  Add-on          ├────────►│  Assistant  │
│  Serial Mode    │ Serial  │  (Bridge)        │         │             │
└─────────────────┘         └──────────────────┘         └─────────────┘
```

Your Pico sends serial2mqtt JSON messages over USB. The add-on translates them to/from MQTT topics that Home Assistant understands.

## Protocol Example

**Device publishes pattern change:**
```json
[1, "src/whiteboard/pattern/state", "rainbow", 0, 0]
```
↓ Bridge forwards to MQTT ↓
```
Topic: src/whiteboard/pattern/state
Payload: rainbow
```

**Home Assistant sets brightness:**
```
Topic: dst/whiteboard/brightness/set
Payload: 192
```
↓ Bridge sends to device ↓
```json
[1, "dst/whiteboard/brightness/set", "192"]
```

## Eclipse OS Integration

This add-on works seamlessly with the dual-transport MQTT implementation in Eclipse OS:

**C++ Transport Layer:**
- `MqttTransportWiFi` - For WiFi mode (Pico W)
- `MqttTransportSerial` - For USB mode (any Pico)

Both use the same `MqttClient` interface, so your relics work unchanged!

## Benefits vs WiFi

**Memory Savings (Raspberry Pi Pico):**
- Program: 440KB → 386KB (-54KB)
- RAM: 77KB → 19KB (-58KB)

**Hardware Savings:**
- Use standard Pico ($4) instead of Pico W ($6)
- No WiFi credentials to manage
- More reliable connection

**Use Cases:**
- Devices in metal enclosures (poor WiFi)
- Secure environments (no wireless)
- Distance from WiFi router
- Large number of devices (USB hubs)

## Troubleshooting

**Add-on won't start:**
- Check logs: Settings → Add-ons → Serial2MQTT Bridge → Logs
- Verify serial port exists: `ls /dev/tty*`
- Ensure Mosquitto broker is running

**No data flowing:**
- Test serial connection: `cat /dev/ttyACM0`
- Verify baud rate matches device (default 9600)
- Check MQTT topics: Settings → MQTT → Listen to `src/#`

**See [INSTALLATION.md](INSTALLATION.md) for detailed troubleshooting**

## Publishing to GitHub

Want to share this add-on?

1. Create GitHub repo: `hassio-addon-serial2mqtt`
2. Update `repository.yaml` with your username
3. Push all files
4. Add to HA: Settings → Add-ons → Repositories → Add your GitHub URL

## Version

**Current:** 1.0.0 (2025-11-09)

See [CHANGELOG.md](serial2mqtt/CHANGELOG.md) for version history.

## License

MIT License

## Credits

Built for [Eclipse OS](https://github.com/yourusername/eclipse-os)

Inspired by:
- [vortex314/serial2mqtt](https://github.com/vortex314/serial2mqtt)
- [dgomes/serial2mqtt](https://github.com/dgomes/serial2mqtt)

## Support

- **Installation help:** [INSTALLATION.md](INSTALLATION.md)
- **Add-on documentation:** [serial2mqtt/README.md](serial2mqtt/README.md)
- **Eclipse OS:** https://github.com/yourusername/eclipse-os
- **Issues:** Open an issue on GitHub

---

**Ready to use your spare Pico boards with Home Assistant? Install the add-on and start building!**
