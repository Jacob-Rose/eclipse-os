# AGENTS.md - Eclipse OS Development Guide

## Build/Test Commands
- **Compile**: `./compile.sh [obelisk|whiteboard] [deploy|dev]` (default: obelisk, dev)
- **Upload**: `./upload.sh [obelisk|whiteboard] [deploy|dev] [-p PORT]` - compiles and flashes the first board found
- **Release .uf2**: `tools/build-firmware.sh whiteboard deploy` -> `build-firmware/eclipse-os.whiteboard.deploy.uf2`
- The relic/mode words become `-D`s for the whole build via `tools/firmware-env.sh`; never edit the `#define`s in `eclipse-os.ino` to switch relics
- **After flashing an HA relic**: `tools/check-ha.py` must print `HA CHECK PASS` (drives every advertised MQTT topic from this machine; `--tidy` clears stale discovery). A change to discovery topics, subscriptions or `secrets.h` is not done until it passes
- **Transport bench (board)**: `tools/bench-transports/` is a sketch that loops DMX out into DMX in and MIDI TX into RX on a bare Pico with two jumpers (GP0->GP5, GP0->GP1) and prints PASS/FAIL over serial; build it with `-b rp2040:rp2040:rpipicow:usbstack=tinyusb --library lib` to also get USB MIDI echo
- **Library tests (host)**: `tests/run.sh [substring]` builds the portable library with `USE_ARDUINO=0` and the MinGW/host g++ and runs `tests/*.cpp`; add a test file per subsystem. `fp.h` and `name.h` also pin their contracts with `static_assert`
- **Desktop build (Windows)**: `cmake -S desktop -B desktop/build/review -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc && cmake --build desktop/build/review`; self-tests `desktop/build/review/eclipse-dmx.exe --link-selftest|--blend-selftest|--midi-selftest`; the python suite is `ECLIPSE_DMX_BINARY=<that exe> bash desktop/test.sh` (opens windows, ~9 minutes)
- Strip pins, lengths and colour orders live only in `src/relics/wiring.h` (one line per relic, greppable by the scripts); a relic takes `wiring::kName`, never its own pin number

## Code Style Guidelines
- **File headers**: Include copyright header with "Copyright 2024 | Jake Rose" and license reference
- **Includes**: library headers by module, `<ecore/hsv.h>`, `<eanim/generator_hsv.h>`, from anywhere (a sketch includes `<eclipse-os.h>` once first); same-directory includes in quotes; sketch-internal includes between relics, kits and images stay relative
- **Namespaces**: Use explicit namespace declarations (e.g., `using namespace ecore;`)
- **Standard library**: the library is C++20 on every toolchain. Use `std::lerp`, `std::clamp`, `std::min`/`max` qualified; never `using namespace std` in a header, and never an unqualified `lerp` (the rp2040 core's libstdc++ exports `std::lerp` globally, which makes one ambiguous)
- **Naming**: CamelCase classes, snake_case files/functions, descriptive variable names
- **Memory**: Use smart pointers (`std::unique_ptr`, `std::shared_ptr`) for object management
- **Error handling**: Check bounds, use ERROR_CHECKING_ENABLED preprocessor flag
- **Comments**: headers document how to use the thing, not why it was built that way. A `/* @brief ... */` block on each class, struct and enum, with a short usage note or a code example when the signature does not say enough; `///` one-liners on members; `@see` for cross-references; `@param`/`@return` only where the signature is ambiguous. Design rationale belongs in the commit message or the readme. Inline comments in .cpp files stay minimal and say "why"

## Architecture
- **Structure**: `/lib/` is the library (an Arduino library; sources under `/lib/src/<module>/`), `/src/relics/` is device-specific code and the looks that are content (`mythos26/`, `generic/`), `/src/kits/` is content. The scripts hand `lib/` to arduino-cli by path (`--library`), `sketch.yaml` carries a profile for the IDE and for anyone without the scripts
- **Looks by name**: a new look is registered once in `src/relics/registry.cpp`; the desk's pattern table, `--list-patterns` and the python `pattern_names()` all read the library registry, so nothing else is edited. The desk keeps only the looks that are desk-specific (`desktop/src/pattern.cpp`) and the state machines built from content
- **State pattern**: Inherit from State base classes, implement tick() and init() methods
- **IO abstraction**: Use RelicIO base classes for hardware interaction
- **Patterns**: Factory pattern for state creation, observer pattern for transitions