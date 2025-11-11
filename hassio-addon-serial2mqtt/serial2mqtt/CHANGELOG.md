# Changelog

## [1.0.0] - 2025-11-09

### Added
- Initial release
- Bidirectional serial to MQTT bridge
- Support for serial2mqtt JSON protocol
- Compatible with Eclipse OS serial MQTT transport
- Auto-reconnection for serial and MQTT
- Configurable baud rate and MQTT credentials
- Debug logging for troubleshooting

### Features
- Supports all Home Assistant architectures (amd64, armv7, aarch64, armhf, i386)
- Lightweight Alpine Linux base
- Works with Raspberry Pi Pico and Arduino boards
- MQTT authentication support
- Automatic subscription to device-bound topics (dst/*)
