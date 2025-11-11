# Installation Guide: Serial2MQTT Add-on for Home Assistant

## Quick Start (Local Installation)

This is the fastest way to install and test the add-on on Home Assistant OS.

### Step 1: Copy Files to Home Assistant

You have several options:

#### Option A: Using Samba/SMB Share

1. **Enable Samba add-on** in Home Assistant:
   - Settings → Add-ons → Add-on Store
   - Search for "Samba share" and install
   - Start the add-on

2. **Connect from your computer**:
   - Windows: `\\homeassistant.local`
   - Mac: `smb://homeassistant.local`
   - Linux: `smb://homeassistant.local`

3. **Create folder and copy files**:
   - Navigate to `/addons/`
   - Create folder: `serial2mqtt`
   - Copy all files from `hassio-addon-serial2mqtt/serial2mqtt/` to `/addons/serial2mqtt/`

#### Option B: Using SSH/Terminal

1. **Enable SSH** (Advanced SSH & Web Terminal add-on recommended)

2. **Create directory**:
   ```bash
   mkdir -p /addons/serial2mqtt
   ```

3. **Upload files** using SCP or SFTP:
   ```bash
   # From your computer
   scp -r hassio-addon-serial2mqtt/serial2mqtt/* root@homeassistant.local:/addons/serial2mqtt/
   ```

### Step 2: Add Local Repository

1. Go to **Settings** → **Add-ons** → **Add-on Store**
2. Click the **⋮** menu (top right corner)
3. Select **Repositories**
4. Add this path: `/addons`
5. Click **Add**
6. Close and refresh the page

### Step 3: Install the Add-on

1. Scroll down to **Local add-ons** section
2. Find **"Serial2MQTT Bridge"**
3. Click on it
4. Click **Install** (may take 2-5 minutes)

### Step 4: Configure

1. Go to the **Configuration** tab
2. Edit settings:

```yaml
serial_port: "/dev/ttyACM0"  # Change if needed
baud_rate: 9600              # Must match your device
mqtt_host: "core-mosquitto"  # Default MQTT broker
mqtt_port: 1883
mqtt_user: ""                # Optional
mqtt_password: ""            # Optional
topic_prefix: "dst"
```

3. Click **Save**

### Step 5: Find Your Serial Port

1. SSH into Home Assistant
2. Run: `ls /dev/tty*`
3. Look for:
   - `/dev/ttyACM0` - Arduino/Pico (most common)
   - `/dev/ttyUSB0` - USB serial adapters
   - `/dev/serial/by-id/usb-*` - Persistent names

4. Update `serial_port` in config if different

### Step 6: Start the Add-on

1. Go to **Info** tab
2. Toggle **Start on boot** (optional but recommended)
3. Click **Start**

### Step 7: Check Logs

1. Go to **Log** tab
2. You should see:
   ```
   Starting Serial2MQTT Bridge
   Serial Port: /dev/ttyACM0 @ 9600 baud
   MQTT Broker: core-mosquitto:1883
   Serial port /dev/ttyACM0 opened successfully
   Connected to MQTT broker
   Subscribed to MQTT topic: dst/#
   Bridge running. Press Ctrl+C to exit.
   ```

## Troubleshooting

### "Failed to open serial port"

**Solution**:
- Verify device is plugged in: `ls /dev/tty*`
- Try different port names
- Restart Home Assistant

### "Failed to connect to MQTT broker"

**Solution**:
- Ensure Mosquitto broker add-on is installed and running
- Check `mqtt_host` is correct (usually `core-mosquitto`)
- Verify username/password if using auth

### "Permission denied"

**Solution**:
- The add-on has `uart: true` in config.yaml
- Restart Home Assistant
- Check add-on logs for details

### No data flowing

1. **Check device is sending data**:
   ```bash
   cat /dev/ttyACM0
   ```
   Should see output from your device

2. **Verify MQTT topics**:
   - Settings → Devices & Services → MQTT → Configure
   - Listen to topic: `src/#`
   - Should see messages from your device

3. **Check baud rate matches** between add-on and device

## GitHub Repository Setup (Optional)

To share this add-on or use it across multiple Home Assistant installations:

### Step 1: Create GitHub Repository

1. Create new repository: `hassio-addon-serial2mqtt`
2. Upload the entire `hassio-addon-serial2mqtt` folder structure:
   ```
   hassio-addon-serial2mqtt/
   ├── serial2mqtt/
   │   ├── config.yaml
   │   ├── Dockerfile
   │   ├── build.yaml
   │   ├── run.py
   │   ├── README.md
   │   └── CHANGELOG.md
   └── repository.yaml
   ```

### Step 2: Update repository.yaml

Edit `repository.yaml` with your GitHub username:
```yaml
name: "Eclipse OS Add-ons"
url: "https://github.com/YOUR_USERNAME/hassio-addon-serial2mqtt"
maintainer: "Your Name"
```

### Step 3: Add to Home Assistant

1. Settings → Add-ons → Add-on Store → ⋮ → Repositories
2. Add: `https://github.com/YOUR_USERNAME/hassio-addon-serial2mqtt`
3. Refresh and install from the store

## Next Steps

Once the add-on is running:

1. **Flash your Pico with Eclipse OS** (with `USE_SERIAL_MQTT 1`)
2. **Connect via USB** to Home Assistant
3. **Check MQTT** - device should auto-discover in Home Assistant
4. **Control your device** - patterns, brightness, power via HA UI

## File Structure Reference

```
hassio-addon-serial2mqtt/
├── repository.yaml           # Repository metadata
├── INSTALLATION.md          # This file
└── serial2mqtt/             # Add-on folder
    ├── config.yaml          # Add-on configuration
    ├── Dockerfile           # Container build instructions
    ├── build.yaml           # Multi-arch build config
    ├── run.py               # Main Python script
    ├── README.md            # Documentation
    └── CHANGELOG.md         # Version history
```

## Support

- Eclipse OS: https://github.com/yourusername/eclipse-os
- Home Assistant Community: https://community.home-assistant.io
- Add-on Issues: Open an issue on the GitHub repository
