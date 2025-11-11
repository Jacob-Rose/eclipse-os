#!/bin/bash

# Find the first connected USB device for this Arduino environment
DEVICE=$(arduino-cli board list | grep -E "(ttyACM|ttyUSB)" | head -1 | awk '{print $1}')

if [ -z "$DEVICE" ]; then
    echo "Error: No Arduino device found!"
    echo ""
    echo "Available ports:"
    arduino-cli board list
    exit 1
fi

echo "Found device: $DEVICE"
echo "Starting monitor..."
echo ""

arduino-cli monitor -p "$DEVICE" --fqbn rp2040:rp2040:rpipicow
