#!/usr/bin/env bash
#
# Hand-patches to installed Arduino libraries that eclipse-os needs.
#
# These are bugs in third-party code, not in this repo, so they cannot be fixed
# here - and they do not survive a library reinstall or an arduino-cli upgrade.
# The readme used to describe them in prose; this applies them, is idempotent,
# and tells you which ones were already fine so you can find out when upstream
# has fixed one.
#
#   tools/patch-libraries.sh
#
# Verified against: arduino-cli 1.x, rp2040 core 6.0.0, AnimatedGIF 2.2.0.

set -u

LIBS="${ARDUINO_LIBS:-$HOME/Arduino/libraries}"
applied=0
skipped=0

note()  { echo "  $*"; }
ok()    { echo "OK    $*"; }
did()   { echo "PATCH $*"; applied=$((applied + 1)); }
none()  { echo "SKIP  $*"; skipped=$((skipped + 1)); }

echo "libraries: $LIBS"
echo

# ---------------------------------------------------------------------------
# AnimatedGIF: millis() / delay() not declared
#
# The core defines PICO_BUILD, so AnimatedGIF.h takes a non-Arduino branch and
# skips <Arduino.h> - while AnimatedGIF.cpp goes on calling millis() and
# delay(). Put the include back when we are plainly building for Arduino.
# ---------------------------------------------------------------------------
GIF="$LIBS/AnimatedGIF/src/AnimatedGIF.h"
if [ ! -f "$GIF" ]; then
    none "AnimatedGIF not installed"
elif grep -q "eclipse-os: restore Arduino.h" "$GIF"; then
    ok "AnimatedGIF already patched"
else
    # Right after the include guard, before anything decides it is not Arduino.
    marker=$(grep -n '#define __ANIMATEDGIF__' "$GIF" | head -1 | cut -d: -f1)
    if [ -z "$marker" ]; then
        none "AnimatedGIF: could not find its include guard; patch by hand"
    else
        cp "$GIF" "$GIF.eclipse-os.bak"
        awk -v at="$marker" 'NR==at {
            print
            print ""
            print "// eclipse-os: restore Arduino.h. The rp2040 core defines PICO_BUILD, so the"
            print "// branch below skips this include while AnimatedGIF.cpp still calls millis()"
            print "// and delay(). See tools/patch-libraries.sh."
            print "#if defined(ARDUINO)"
            print "#include <Arduino.h>"
            print "#endif"
            next
        } { print }' "$GIF.eclipse-os.bak" > "$GIF"
        did "AnimatedGIF.h: <Arduino.h> restored (backup at $GIF.eclipse-os.bak)"
    fi
fi

# ---------------------------------------------------------------------------
# SPI / SPIHelper
#
# These were needed while eio/relic.h did `using namespace std;` at global
# scope, which put std::byte in scope wherever it had been included first and
# made Arduino's `byte` ambiguous inside SPI.h. That header no longer opens std,
# so the patches should be unnecessary - checked rather than assumed, because
# the failure mode is an error deep inside a core header with no obvious cause.
# ---------------------------------------------------------------------------
CORE=$(ls -d "$HOME"/.arduino15/packages/rp2040/hardware/rp2040/* 2>/dev/null | tail -1)
if [ -n "${CORE:-}" ] && [ -f "$CORE/libraries/SPI/src/SPIHelper.h" ]; then
    if head -3 "$CORE/libraries/SPI/src/SPIHelper.h" | grep -q "#pragma once"; then
        ok "SPIHelper.h already has #pragma once"
    else
        none "SPIHelper.h has no #pragma once - only matters if it gets included twice"
    fi
else
    none "rp2040 core SPI not found"
fi

echo
echo "$applied applied, $skipped skipped"
