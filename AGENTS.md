# AGENTS.md - Eclipse OS Development Guide

## Build/Test Commands
- **Compile**: `arduino-cli compile -b rp2040:rp2040:rpipico eclipse-os.ino`
- **Upload**: `arduino-cli upload -p /dev/ttyACM0 --fqbn rp2040:rp2040:rpipico eclipse-os.ino --verbose`
- **Quick compile**: `./compile.sh`
- **Quick upload**: `./upload.sh`

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