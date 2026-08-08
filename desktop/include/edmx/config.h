// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>
#include <vector>

#include "lib/ecore/hsv.h"

#include "edmx/fixture.h"

///
/// The config file. This is the contract between the python wrapper and the
/// executable, and the only input the executable needs to light a rig.
///

namespace edmx
{
    struct DeviceConfig
    {
        std::string type{"enttec_pro"}; ///< enttec_pro | enttec_open | console
        std::string port{"auto"};       ///< "auto", "COM3", "/dev/ttyUSB0"
        int baud{115200};               ///< PRO link speed; ignored by enttec_open
        float fps{40.0f};               ///< DMX refresh rate; 40 is the spec's max
        int consoleChannels{12};        ///< how many channels the console output prints
    };

    struct MasterConfig
    {
        float brightness{1.0f};
        float gamma{2.2f};
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
    };

    struct Config
    {
        DeviceConfig device;
        MasterConfig master;
        PatternConfig pattern;
        FixtureMap fixtures;

        /// Warnings raised during load. Non-fatal: reported, then we light up.
        std::vector<std::string> warnings;
    };

    /// Parses the config file at `path`. Returns false with `outError` set on a
    /// problem that would leave us unable to output at all (bad JSON, no
    /// fixtures, an unusable channel order).
    bool loadConfig(const std::string& path, Config& outConfig, std::string& outError);

    /// Parses a colour written as "#rrggbb", "rrggbb", or an
    /// {"h":..,"s":..,"v":..} object. Returns false when it is neither.
    bool parseColorString(const std::string& text, ecore::HSV& outColor);

    /// Looks up a palette from kits/palettes.h by name. Returns false when the
    /// name is not one of the built-ins.
    bool lookupNamedPalette(const std::string& name, ecore::HSVPalette& outPalette);

    /// Names of every built-in palette, for --list-palettes and error messages.
    std::vector<std::string> builtinPaletteNames();
}
