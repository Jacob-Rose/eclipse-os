# AGENTS.md - Eclipse OS Development Guide

## Build/Test Commands
- **Compile**: `./compile.sh [obelisk|whiteboard] [deploy|dev]` (default: obelisk, dev)
- **Upload**: `./upload.sh [obelisk|whiteboard] [deploy|dev] [-p PORT]` - compiles and flashes the first board found
- **Release .uf2**: `tools/build-firmware.sh whiteboard deploy` -> `build-firmware/eclipse-os.whiteboard.deploy.uf2`
- The relic/mode words become `-D`s for the whole build via `tools/firmware-env.sh`; never edit the `#define`s in `eclipse-os.ino` to switch relics
- **After flashing an HA relic**: `tools/check-ha.py` must print `HA CHECK PASS` (drives every advertised MQTT topic from this machine; `--tidy` clears stale discovery). A change to discovery topics, subscriptions or `secrets.h` is not done until it passes
- **Library tests (host)**: `tests/run.sh [substring]` builds the portable library with `USE_ARDUINO=0` and the MinGW/host g++ and runs `tests/*.cpp`; add a test file per subsystem. `fp.h` and `name.h` also pin their contracts with `static_assert`
- **Desktop build (Windows)**: `cmake -S desktop -B desktop/build/review -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc && cmake --build desktop/build/review`; self-tests `desktop/build/review/eclipse-dmx.exe --link-selftest|--blend-selftest|--midi-selftest`; the python suite is `ECLIPSE_DMX_BINARY=<that exe> bash desktop/test.sh` (opens windows, ~9 minutes)
- Strip pins, lengths and colour orders live only in `src/relics/wiring.h` (one line per relic, greppable by the scripts); a relic takes `wiring::kName`, never its own pin number

## Code Style Guidelines
- **File headers**: Include copyright header with "Copyright 2024 | Jake Rose" and license reference
- **Includes**: Use relative paths with "../../" for lib includes, group by category
- **Namespaces**: Use explicit namespace declarations (e.g., `using namespace ecore;`)
- **Standard library**: the library is C++20 on every toolchain. Use `std::lerp`, `std::clamp`, `std::min`/`max` qualified; never `using namespace std` in a header, and never an unqualified `lerp` (the rp2040 core's libstdc++ exports `std::lerp` globally, which makes one ambiguous)
- **Naming**: CamelCase classes, snake_case files/functions, descriptive variable names
- **Memory**: Use smart pointers (`std::unique_ptr`, `std::shared_ptr`) for object management
- **Error handling**: Check bounds, use ERROR_CHECKING_ENABLED preprocessor flag
- **Comments**: headers document how to use the thing, not why it was built that way. A `/* @brief ... */` block on each class, struct and enum, with a short usage note or a code example when the signature does not say enough; `///` one-liners on members; `@see` for cross-references; `@param`/`@return` only where the signature is ambiguous. Design rationale belongs in the commit message or the readme. Inline comments in .cpp files stay minimal and say "why"

## Architecture
- **Structure**: `/src/lib/` for core libraries, `/src/relics/` for device-specific code
- **State pattern**: Inherit from State base classes, implement tick() and init() methods
- **IO abstraction**: Use RelicIO base classes for hardware interaction
- **Patterns**: Factory pattern for state creation, observer pattern for transitions