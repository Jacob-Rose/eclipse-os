# Copyright 2024 | Jake Rose
#
# This file is part of project eclipse-os
# See readme.md for full license details.
#
# Desktop build: eclipse-dmx, the executable that renders eclipse-os patterns
# to an Enttec USB DMX widget on Windows and Linux.
#
# This deliberately does NOT include the root CMakeLists. The microcontroller
# build has its own toolchain story; this one only wants the portable slice of
# the library, compiled with USE_ARDUINO=0.

cmake_minimum_required(VERSION 3.16)
project(eclipse-dmx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(ECLIPSE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(ECLIPSE_SRC  "${ECLIPSE_ROOT}/src")

# ---------------------------------------------------------------------------
# The portable slice of eclipse-os.
#
# Listed explicitly rather than globbed: everything here has been checked to
# build off-Arduino, and a glob would quietly pull in emqtt/ewifi the moment
# someone adds a file.
# ---------------------------------------------------------------------------
set(ECLIPSE_CORE_SOURCES
    ${ECLIPSE_SRC}/lib/ecore/coord.cpp
    ${ECLIPSE_SRC}/lib/ecore/fp.cpp
    ${ECLIPSE_SRC}/lib/ecore/hsv.cpp
    ${ECLIPSE_SRC}/lib/ecore/logging.cpp
    ${ECLIPSE_SRC}/lib/ecore/math.cpp
    ${ECLIPSE_SRC}/lib/ecore/property.cpp
    ${ECLIPSE_SRC}/lib/ecore/range.cpp
    ${ECLIPSE_SRC}/lib/ecore/tickable.cpp
    ${ECLIPSE_SRC}/lib/ecore/timer.cpp
    ${ECLIPSE_SRC}/lib/ecore/utils.cpp

    ${ECLIPSE_SRC}/lib/eanim/attribute_float.cpp
    ${ECLIPSE_SRC}/lib/eanim/automation_curve.cpp
    ${ECLIPSE_SRC}/lib/eanim/generator_float.cpp
    ${ECLIPSE_SRC}/lib/eanim/generator_hsv.cpp
    ${ECLIPSE_SRC}/lib/eanim/lfo.cpp
    ${ECLIPSE_SRC}/lib/eanim/noise.cpp
    ${ECLIPSE_SRC}/lib/eanim/processor_float.cpp

    ${ECLIPSE_SRC}/lib/eio/hsv_strip.cpp
    ${ECLIPSE_SRC}/lib/eio/relic.cpp
    ${ECLIPSE_SRC}/lib/eio/screen_drawer.cpp
    ${ECLIPSE_SRC}/lib/eio/strip_projection.cpp

    ${ECLIPSE_SRC}/lib/esm/state.cpp
    ${ECLIPSE_SRC}/lib/esm/state_generic.cpp

    ${ECLIPSE_SRC}/lib/external/easing.cpp

    ${ECLIPSE_SRC}/kits/effects.cpp
)

# Relic *pattern* sources built for the host.
#
# The patterns only - not the relic's device/IO layer. obelisk.cpp (ObeliskIO,
# RelicCore, the neopixel strip segments) is deliberately absent: the look is
# the portable part, and here it renders through our own fixture patch instead
# of a physical strip.
#
# These are the actual relic patterns, not copies. A pattern is a GeneratorHSV,
# so anything written for a relic runs here through edmx::GeneratorPattern with
# no porting at all. Add a pattern source here, register it in pattern.cpp,
# done.
set(ECLIPSE_RELIC_SOURCES
    ${ECLIPSE_SRC}/relics/obelisk/state_obelisk.cpp

    # The jacket's looks. Its State_* wrappers reach into JacketIO for physical
    # buttons, so the desktop drives the Pattern_* generators inside them
    # directly, through esm's own State_GenericHSV - the state machine is real,
    # only the input source differs. The wrappers still compile here; they are
    # simply never instantiated.
    ${ECLIPSE_SRC}/relics/pendant/pendant_generic_state.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_bluemagic.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_breathewithme.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_campfire.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_cybertoxin.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_datamine.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_digitalvoid.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_enchantedforest.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_hitstop.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_parrot.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_rainbowroad.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_systemoverload.cpp
    ${ECLIPSE_SRC}/relics/jacket/visual/state_warpturbines.cpp

    # Buttons never fire on a host - digitalRead is shimmed to "not pressed" -
    # but the jacket states link against them.
    ${ECLIPSE_SRC}/lib/eio/button.cpp
)

list(APPEND ECLIPSE_CORE_SOURCES ${ECLIPSE_RELIC_SOURCES})

# Still out of the host build:
#   emqtt/, ewifi/    - hard Arduino dependencies (WiFi.h, PubSubClient).
#   imgs/, screen     - the gif/display path needs AnimatedGIF and the GC9A01A
#                       driver. screen_drawer compiles to nothing when
#                       USE_SCREEN is 0, which is what lets eio::Relic build.

add_library(eclipse_core STATIC ${ECLIPSE_CORE_SOURCES})

target_include_directories(eclipse_core PUBLIC
    ${ECLIPSE_SRC}
)

# USE_ARDUINO=0 is what swaps in ecore/platform_host.h and drops the neopixel
# driver out of HSVStrip. Everything else about the library is unchanged.
target_compile_definitions(eclipse_core PUBLIC
    USE_ARDUINO=0
    USE_PICO_SDK=0
    ERROR_CHECKING_ENABLED=1
    DEBUG_LOGGING_ENABLED=1
)

# ---------------------------------------------------------------------------
# edmx: the DMX output layer
# ---------------------------------------------------------------------------
add_library(edmx STATIC
    src/beat_clock.cpp
    src/config.cpp
    src/dmx_output.cpp
    src/fixture.cpp
    src/json.cpp
    src/midi_input.cpp
    src/mythos26.cpp
    src/pattern.cpp
    src/serial_port.cpp
    src/state_machine.cpp
)

target_include_directories(edmx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(edmx PUBLIC eclipse_core)

# ---------------------------------------------------------------------------
# the executable
# ---------------------------------------------------------------------------
add_executable(eclipse-dmx src/main.cpp)
target_link_libraries(eclipse-dmx PRIVATE edmx)

# ---------------------------------------------------------------------------
# platform bits
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
target_link_libraries(edmx PUBLIC Threads::Threads)

if(WIN32)
    # RegOpenKeyEx / RegEnumValue for port enumeration
    target_link_libraries(edmx PUBLIC advapi32)
    # midiIn* for tempo in. winmm is part of Windows; nothing to install.
    target_link_libraries(edmx PUBLIC winmm)
endif()

if(MINGW)
    # Static-link the gcc runtime so the .exe runs on a machine that has never
    # seen MinGW. This is a tool that gets copied to a show laptop, and
    # "missing libstdc++-6.dll" is not a useful thing to debug at load-in.
    target_link_options(eclipse-dmx PRIVATE
        -static-libgcc
        -static-libstdc++
        -static
    )
endif()

if(MSVC)
    target_compile_options(eclipse_core PRIVATE /W3)
    target_compile_options(edmx PRIVATE /W3)
    # the library leans on a few things MSVC flags as unsafe-but-standard
    target_compile_definitions(eclipse_core PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(edmx PRIVATE -Wall -Wextra)

    # FastNoiseLite's 3D cellular path trips -Waggressive-loop-optimizations at
    # -O2. It is vendored third-party code and we only ever call the 2D entry
    # points, so silence it here rather than editing someone else's header.
    target_compile_options(eclipse_core PRIVATE -Wno-aggressive-loop-optimizations)
endif()

install(TARGETS eclipse-dmx RUNTIME DESTINATION bin)
