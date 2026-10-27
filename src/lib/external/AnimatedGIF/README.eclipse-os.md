# AnimatedGIF, vendored

Larry Bank's AnimatedGIF 2.2.0 (https://github.com/bitbank2/AnimatedGIF),
Apache-2.0, see LICENSE. Only the library's `src/` is here: `AnimatedGIF.h`,
`AnimatedGIF.cpp` and `gif_impl.h` (upstream `gif.inl`, renamed because the Arduino build copies only .h/.cpp sources).

Vendored rather than installed because upstream's header skips `<Arduino.h>`
whenever `PICO_BUILD` is defined, which the arduino-pico core does, and then
`AnimatedGIF.cpp` calls `millis()` and `delay()` undeclared. The one change
from upstream is the include block at the top of `AnimatedGIF.h`, marked
`eclipse-os`. Every include in the tree names this copy by relative path, so an
AnimatedGIF installed in the Arduino libraries folder is never picked up -
if one were, the duplicate class would fail the link rather than build wrong.

To update: copy upstream's three files over these and re-apply that block.
