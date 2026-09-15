# AGENTS.md - Eclipse OS Development Guide

## Build/Test Commands
- **Compile**: `./compile.sh [obelisk|whiteboard] [deploy|dev]` (default: obelisk, dev)
- **Upload**: `./upload.sh [obelisk|whiteboard] [deploy|dev] [-p PORT]` - compiles and flashes the first board found
- **Release .uf2**: `tools/build-firmware.sh whiteboard deploy` -> `build-firmware/eclipse-os.whiteboard.deploy.uf2`
- The relic/mode words become `-D`s for the whole build via `tools/firmware-env.sh`; never edit the `#define`s in `eclipse-os.ino` to switch relics
- **After flashing an HA relic**: `tools/check-ha.py` must print `HA CHECK PASS` (drives every advertised MQTT topic from this machine; `--tidy` clears stale discovery). A change to discovery topics, subscriptions or `secrets.h` is not done until it passes
- Strip pins, lengths and colour orders live only in `src/relics/wiring.h` (one line per relic, greppable by the scripts); a relic takes `wiring::kName`, never its own pin number

## Code Style Guidelines
- **File headers**: Include copyright header with "Copyright 2024 | Jake Rose" and license reference
- **Includes**: Use relative paths with "../../" for lib includes, group by category
- **Namespaces**: Use explicit namespace declarations (e.g., `using namespace ecore;`)
- **Naming**: CamelCase classes, snake_case files/functions, descriptive variable names
- **Memory**: Use smart pointers (`std::unique_ptr`, `std::shared_ptr`) for object management
- **Error handling**: Check bounds, use ERROR_CHECKING_ENABLED preprocessor flag
- **Comments**: Minimal inline comments, focus on "why" not "what"

## Architecture
- **Structure**: `/src/lib/` for core libraries, `/src/relics/` for device-specific code
- **State pattern**: Inherit from State base classes, implement tick() and init() methods
- **IO abstraction**: Use RelicIO base classes for hardware interaction
- **Patterns**: Factory pattern for state creation, observer pattern for transitions