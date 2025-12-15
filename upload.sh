#!/bin/bash
# Upload script with automatic reset support for RP2040
# Uses 1200bps touch to trigger bootloader mode
arduino-cli upload -p /dev/ttyACM0 --fqbn rp2040:rp2040:rpipicow eclipse-os.ino --verbose --verify
