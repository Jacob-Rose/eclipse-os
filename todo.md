# To do

## Library
- A global tick manager with an order of operations, instead of each relic's
  core ticking its own pieces in its own order.
- Make `Saw` work on negative numbers.
- Crash output over serial: today a hard fault is silent, and a picoprobe is
  the only way to see it. A panic handler that prints the fault and the last
  log lines would cover most of it.
- Looks with an alpha channel: a layer says how much of a node it claims, so
  the desk can send the sculpture an overlay with a claim per pixel and the
  relic composes it over its own look. That retires `RelicShadow`, the `sim`
  handshake and `PatternScanner::ObeliskMode`. Needs the state machine to be
  a `GeneratorHSV` so a whole machine can be one layer.
- A registry of relics, their looks and their states, so the desk discovers
  them instead of keeping its own table in `desktop/src/pattern.cpp`.
- Declarative property tables (member-pointer lists) as an alternative to
  hand-written `reflect()` bodies, once the registry exists to use them.
- Only compile the screen libraries for relics that have a screen:
  `USE_SCREEN` is on for every Arduino build today, so the obelisk and the
  whiteboard carry the GIF and display code they never use.

## Firmware, on a bench
- `edmx::UartDmxOutput` and `UartDmxInput`, `emidi::UartMidiTransport` and
  `UsbMidiTransport` compile but have not run on a board. The DMX break and
  mark timing wants a scope or a fixture; USB MIDI needs a build on the
  TinyUSB stack.
- No relic declares a DMX or MIDI port in `relics/wiring.h` yet.

## Looks
- Laser scan (terminator scan).
