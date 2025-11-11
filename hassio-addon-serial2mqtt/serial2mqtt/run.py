#!/usr/bin/env python3
"""
Serial2MQTT Bridge for Home Assistant
Bridges serial port communication to MQTT using the serial2mqtt JSON protocol
"""

import serial
import json
import paho.mqtt.client as mqtt
import time
import logging
import sys
import os

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    handlers=[logging.StreamHandler(sys.stdout)]
)
logger = logging.getLogger(__name__)

# Load configuration from add-on options
CONFIG_PATH = '/data/options.json'

def load_config():
    """Load configuration from Home Assistant add-on options"""
    try:
        with open(CONFIG_PATH) as f:
            return json.load(f)
    except Exception as e:
        logger.error(f"Failed to load config: {e}")
        sys.exit(1)

config = load_config()

# Configuration
SERIAL_PORT = config.get('serial_port', '/dev/ttyACM0')
BAUD_RATE = config.get('baud_rate', 9600)
MQTT_HOST = config.get('mqtt_host', 'localhost')
MQTT_PORT = config.get('mqtt_port', 1883)
MQTT_USER = config.get('mqtt_user', '')
MQTT_PASS = config.get('mqtt_password', '')
TOPIC_PREFIX = config.get('topic_prefix', 'dst')

logger.info(f"Starting Serial2MQTT Bridge")
logger.info(f"Serial Port: {SERIAL_PORT} @ {BAUD_RATE} baud")
logger.info(f"MQTT Broker: {MQTT_HOST}:{MQTT_PORT}")

# Initialize serial connection
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    logger.info(f"Serial port {SERIAL_PORT} opened successfully")
except Exception as e:
    logger.error(f"Failed to open serial port {SERIAL_PORT}: {e}")
    sys.exit(1)

# Initialize MQTT client
client = mqtt.Client(client_id="serial2mqtt_bridge")

def on_connect(client, userdata, flags, rc):
    """MQTT connection callback"""
    if rc == 0:
        logger.info("Connected to MQTT broker")
        # Subscribe to all destination topics
        subscribe_topic = f"{TOPIC_PREFIX}/#"
        client.subscribe(subscribe_topic)
        logger.info(f"Subscribed to MQTT topic: {subscribe_topic}")
    else:
        logger.error(f"Failed to connect to MQTT broker, return code {rc}")

def on_disconnect(client, userdata, rc):
    """MQTT disconnection callback"""
    if rc != 0:
        logger.warning(f"Unexpected MQTT disconnection. Will auto-reconnect")

def on_message(client, userdata, msg):
    """Handle incoming MQTT messages and forward to serial"""
    try:
        topic = msg.topic
        payload = msg.payload.decode('utf-8')

        logger.debug(f"MQTT → Serial: {topic} = {payload}")

        # Format as serial2mqtt JSON array: [cmd, topic, payload, qos, retained]
        serial_msg = json.dumps([1, topic, payload, 0, 0]) + '\n'
        ser.write(serial_msg.encode('utf-8'))

    except Exception as e:
        logger.error(f"Error forwarding MQTT to serial: {e}")

# Set MQTT callbacks
client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_message = on_message

# Connect to MQTT broker
if MQTT_USER and MQTT_PASS:
    client.username_pw_set(MQTT_USER, MQTT_PASS)

try:
    client.connect(MQTT_HOST, MQTT_PORT, 60)
except Exception as e:
    logger.error(f"Failed to connect to MQTT broker: {e}")
    sys.exit(1)

# Start MQTT loop in background
client.loop_start()

# Main loop: Read from serial and publish to MQTT
logger.info("Bridge running. Press Ctrl+C to exit.")
receive_buffer = ""

try:
    while True:
        if ser.in_waiting:
            # Read data from serial
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            receive_buffer += data

            # Process complete lines
            while '\n' in receive_buffer or '\r' in receive_buffer:
                # Split on newline
                if '\n' in receive_buffer:
                    line, receive_buffer = receive_buffer.split('\n', 1)
                else:
                    line, receive_buffer = receive_buffer.split('\r', 1)

                line = line.strip()

                if not line:
                    continue

                # Check if it's a serial2mqtt JSON message
                if line.startswith('['):
                    try:
                        data = json.loads(line)

                        if len(data) >= 3:
                            cmd = data[0]
                            topic = data[1]
                            payload = str(data[2])

                            # Command 1 = Publish
                            if cmd == 1:
                                retained = data[4] if len(data) > 4 else False

                                logger.debug(f"Serial → MQTT: {topic} = {payload}")
                                client.publish(topic, payload, retain=retained)

                            # Command 0 = Subscribe (handled by MQTT client)
                            elif cmd == 0:
                                logger.debug(f"Device subscribed to: {topic}")
                                client.subscribe(topic)

                    except json.JSONDecodeError:
                        # Not a valid JSON message, could be debug output
                        logger.debug(f"Serial debug: {line}")
                    except Exception as e:
                        logger.error(f"Error processing serial message: {e}")
                else:
                    # Non-JSON line, likely debug output from device
                    logger.debug(f"Serial output: {line}")

        time.sleep(0.01)  # Small delay to prevent CPU spinning

except KeyboardInterrupt:
    logger.info("Shutting down Serial2MQTT Bridge")
except Exception as e:
    logger.error(f"Unexpected error: {e}")
finally:
    client.loop_stop()
    client.disconnect()
    ser.close()
    logger.info("Serial2MQTT Bridge stopped")
