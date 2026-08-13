// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "lib/ecore/coord.h"
#include "lib/ecore/hsv.h"

#include "edmx/fixture.h"

///
/// The config file. This is the contract between the python wrapper and the
/// executable, and the only input the executable needs to light a rig.
///

namespace edmx
{
    /// Where the frames go. One per device.
    ///
    /// Named `output` because `device` now means the physical thing this drives
    /// - a sculpture, a truss of pars - and a device has an output the way it
    /// has a name. Config files may still write `"device"` here; see loadConfig.
    struct OutputConfig
    {
        std::string type{"enttec_pro"}; ///< enttec_pro | enttec_open | relic_usb | console | preview
        std::string port{"auto"};       ///< "auto", "COM3", "/dev/ttyUSB0"
        /// PRO link speed; ignored by enttec_open, which is always 250000.
        ///
        /// 115200 is what the PRO's firmware expects on its USB side, and it
        /// is not negotiable: the widget has a microcontroller behind the FTDI
        /// reading at a fixed rate, so a "faster" link just hands it garbage.
        /// Raising this to 250000 does not speed the rig up, it makes the
        /// widget mis-parse frames and the fixtures flicker.
        ///
        /// The rate that actually matters is how many bytes we put in a frame;
        /// see setUniverseLength. Trimmed to a real rig's channel count, a
        /// frame is well under a millisecond of link time even at 115200.
        int baud{115200};
        float fps{40.0f};               ///< DMX refresh rate; 40 is the spec's max
        int consoleChannels{12};        ///< how many channels the console output prints
    };

    struct MasterConfig
    {
        float brightness{1.0f};
        float gamma{2.2f};
    };

    /// Where the beat comes from. See edmx/beat_clock.h and edmx/midi_input.h.
    ///
    /// Everything here is optional. With no `midi` block at all the rig runs on
    /// its own internal tempo, which is what you want at a bench and is a
    /// perfectly serviceable fallback on stage.
    struct MidiConfig
    {
        /// Off by default: a lighting binary should not go opening MIDI devices
        /// on a machine that never asked it to.
        bool enabled{false};

        /// "auto", a device index, or a fragment of a device name. See
        /// MidiInput::resolvePort for what "auto" will and will not guess at.
        std::string port{"auto"};

        /// Name fragments "auto" must never open.
        ///
        /// This exists for the DJ controller on the same machine. It is a MIDI
        /// input, it is often the only one, and its pads and jogs all send
        /// notes — so it is both what "auto" would reach for and the last thing
        /// that should be allowed to move the beat. Naming a port explicitly
        /// still overrides this.
        std::vector<std::string> ignore;

        /// Follow 0xF8 beat clock.
        bool followClock{true};
        /// Take the beat from a note. If a source sends both, notes win.
        bool followNotes{true};

        /// Which note is the beat, and which carries the tempo as velocity+50.
        /// These default to Mixxx's numbering; -1 on beatNote means any note,
        /// and -1 on bpmNote means measure the tempo instead of being told it.
        /// See the message table in edmx/midi_input.h.
        int beatNote{50};
        int bpmNote{52};

        /// Which notes carry the loudness signals, as velocity 0..127. -1 on
        /// any of them ignores it.
        ///
        /// All three are read and kept apart, because they behave differently
        /// and a look picks the one it wants by name — see edmx::VuSource.
        /// These are Mixxx's numbering: 64 instantaneous (every 40ms, peaks on
        /// every kick), 68 the two-second average (the loudness of the track),
        /// 69 the first meter bar (quantised).
        int vuInstantNote{64};
        int vuAverageNote{68};
        int vuMeterNote{69};

        /// Only take beats from this channel, 1..16. -1 means any.
        int beatChannel{-1};

        /// Tempo before anything external has been heard, and the tempo the rig
        /// falls back to when the link goes quiet.
        float bpm{128.0f};

        /// Keep predicting beats after the external clock stops. A drifting rig
        /// beats a dark one.
        bool freeRun{true};
    };

    struct PatternConfig
    {
        std::string name{"palette_wave"};
        float speed{0.25f};
        float width{2.0f};
        float brightness{1.0f};

        /// Explicit palette stops. Empty means "use paletteName".
        std::vector<ecore::HSV> palette;
        /// One of the built-ins from kits/palettes.h.
        std::string paletteName{"p_bluemagic"};

        /// Fixed colour for the `solid` pattern.
        ecore::HSV solidColor{0.0f, 1.0f, 1.0f};

        /// Which look a state machine pattern opens on. Empty means the
        /// machine's own first state, which for the jacket is `digital_void` —
        /// nearly black by design, and a poor opening frame on a par rig.
        std::string stateName;

        /// Overrides for the coordinate frame a pattern is rendered in. Unset
        /// means "whatever the pattern asked for", which is almost always
        /// right — a relic look knows the space it was tuned in. See
        /// edmx::CoordFrame.
        std::optional<float> coordOriginX;
        std::optional<float> coordOriginY;
        std::optional<float> coordSpanX;
        std::optional<float> coordSpanY;
    };

    /// How the config numbers DMX channels.
    ///
    /// DMX512 sends a start code then 512 data slots, and the standard numbers
    /// those slots 1..512. Plenty of fixtures label their address dial 0..511
    /// instead, so a rig's first light reads as 0 and the next as 8.
    ///
    /// This only changes how the *config* is read. Internally, and on the wire,
    /// slot numbering never moves: a fixture at zero-based 0 and one at
    /// one-based 1 produce byte-for-byte identical output.
    enum class Addressing
    {
        OneBased, ///< first slot is 1 (the DMX512 convention, and the default)
        ZeroBased ///< first slot is 0 (what many fixture displays show)
    };

    /// How the config's `position` fields are read.
    ///
    /// Same shape of decision as Addressing: it changes what the numbers in the
    /// *file* mean, and nothing downstream of that.
    enum class CoordSpace
    {
        /// Positions are arbitrary units describing where fixtures sit relative
        /// to each other. They get normalised to 0..1 along the rig and then
        /// stretched across whatever coordinate frame the pattern asked for.
        /// This is what a truss of pars wants, and the default.
        Normalized,

        /// Positions are already in the pattern's coordinate space, and reach
        /// the pattern untouched. This is what a relic wants: the obelisk's
        /// config states the same 0..7 by 0..43 grid its firmware builds, so a
        /// look renders on the desk exactly as it renders on the sculpture.
        Literal
    };

    /// Where a device sits in the pattern's coordinate space.
    ///
    /// A pattern renders into one space and knows nothing about how many
    /// physical objects are in it. This is how an environment says "the pillar
    /// is here and the truss is over there" - so a look that sweeps, or that
    /// keys off a coordinate, does something coherent across both instead of
    /// running twice from scratch.
    ///
    /// Applied as `world = local * scale + offset`, where `local` is whatever
    /// the device's own coordinate space produced. Scale first, so an offset
    /// always means the same thing whatever the device's own units were.
    struct Placement
    {
        float offsetX{0.0f};
        float offsetY{0.0f};
        float scaleX{1.0f};
        float scaleY{1.0f};

        /// "Make this device's own extent exactly this big", instead of stating
        /// a scale.
        ///
        /// The obelisk is 8 by 43 in its own units. Putting it into a pattern
        /// that thinks in 0..1 means a scale of 0.125 by 0.023256, which is a
        /// pair of magic numbers that stop being right the moment the sculpture
        /// is rewired. `"fit": [1, 1]` says the same thing and stays true.
        ///
        /// Resolved once the pattern's coordinate frame is known, because a
        /// normalized device's extent depends on it. Offset then places the
        /// fitted box's corner, so `fit` and `offset` compose the way you would
        /// expect: fit it, then put it somewhere.
        std::optional<float> fitWidth;
        std::optional<float> fitHeight;

        /// How big this device's panel is drawn, relative to an even share of
        /// the viewer's window.
        ///
        /// Purely a view property: it does not touch a single coordinate the
        /// pattern sees, and nothing in this executable reads it beyond
        /// reporting it in --show-patch. It lives here so the schema has one
        /// definition rather than two, the same as everything else.
        ///
        /// Explicit rather than inferred from the geometry. A pillar and a
        /// truss plainly want different panel shapes, but *how* different is a
        /// judgement about the room and the screen - something you tune by
        /// looking at the window, not something a fixture list can tell you.
        float viewScaleX{1.0f};
        float viewScaleY{1.0f};

        ecore::Coordinate apply(float x, float y) const
        {
            return ecore::Coordinate{x * scaleX + offsetX, y * scaleY + offsetY};
        }
    };

    /// One physical thing the desk can light.
    ///
    /// A device owns its geometry, how its numbers are read, and its own way
    /// onto a wire. It does *not* own a pattern or a tempo - those belong to
    /// the show, and every device in an environment shares them. That is the
    /// whole point: one look, rendered once, landing on everything at once.
    ///
    /// Device definitions live in their own files under `devices/` so a
    /// sculpture is described once and referred to by name from every show it
    /// appears in.
    struct Device
    {
        std::string name;
        /// The file this came from, so an error can say which.
        std::string source;

        Addressing addressing{Addressing::OneBased};
        CoordSpace coordSpace{CoordSpace::Normalized};

        OutputConfig output;
        FixtureMap fixtures;
        Placement placement;

        /// Per-device trim, on top of the show's master.
        float brightness{1.0f};
    };

    /// An environment: the devices in a room, and the show running on them.
    ///
    /// Still called Config because it is what a config file loads into, and a
    /// file describing one rig is simply an environment with one device in it -
    /// which is exactly how the legacy single-rig files are read.
    struct Config
    {
        MasterConfig master;
        MidiConfig midi;
        PatternConfig pattern;

        std::vector<Device> devices;

        /// Warnings raised during load. Non-fatal: reported, then we light up.
        std::vector<std::string> warnings;

        /// Fixtures across every device, in device order. This is the order the
        /// pattern renders in and the order the frame stream reports.
        size_t fixtureCount() const;

        /// Where device `index` starts in that run.
        size_t firstFixtureOf(size_t index) const;
    };

    /// Parses the config file at `path`. Returns false with `outError` set on a
    /// problem that would leave us unable to output at all (bad JSON, no
    /// fixtures, an unusable channel order).
    ///
    /// Two shapes are accepted, and which one a file is depends only on whether
    /// it has a `devices` array:
    ///
    ///   environment  `devices: [...]`, each entry naming a device file and
    ///                where it sits. master/pattern/midi belong to the show.
    ///   single rig   `fixtures: [...]` at the top level, with `device` (or
    ///                `output`) beside it. Read as an environment holding one
    ///                unnamed device at the origin, which is what every config
    ///                written before environments existed is.
    ///
    /// Device files referenced by an environment are resolved relative to the
    /// environment's own directory, then to `devices/` beside it.
    bool loadConfig(const std::string& path, Config& outConfig, std::string& outError);

    /// Parses a device definition file into `outDevice`. Environments do this
    /// for each entry; it is exposed for tooling that wants to check one.
    bool loadDevice(const std::string& path, Device& outDevice, std::string& outError,
                    std::vector<std::string>* outWarnings = nullptr);

    /// Parses a colour written as "#rrggbb", "rrggbb", or an
    /// {"h":..,"s":..,"v":..} object. Returns false when it is neither.
    bool parseColorString(const std::string& text, ecore::HSV& outColor);

    /// Looks up a palette from kits/palettes.h by name. Returns false when the
    /// name is not one of the built-ins.
    bool lookupNamedPalette(const std::string& name, ecore::HSVPalette& outPalette);

    /// Names of every built-in palette, for --list-palettes and error messages.
    std::vector<std::string> builtinPaletteNames();
}
