// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "registry.h"

#include <memory>

#include "generic/audio_meter.h"
#include "mythos26/mythos26_looks.h"
#include "obelisk/state_obelisk.h"
#include "scanner/generic_patterns.h"
#include "scanner/osc_patterns.h"
#include "scanner/recording_patterns.h"
#include "scanner/scanner_patterns.h"

namespace
{
    /// Builds a look and inits it when it has an init() to call.
    template <typename LookT>
    std::shared_ptr<eanim::GeneratorHSV> made()
    {
        auto look = std::make_shared<LookT>();
        if constexpr (requires { look->init(); })
        {
            look->init();
        }
        return look;
    }

    template <typename LookT>
    void add(eanim::GeneratorRegistry& registry, const char* name)
    {
        registry.add(name, []() { return made<LookT>(); });
    }
}

void registerRelicGenerators(eanim::GeneratorRegistry& registry)
{
    // obelisk
    add<Pattern_Obelisk_FourSeasons>(registry, "obelisk_seasons");
    add<Pattern_Obelisk_Theater>(registry, "obelisk_theater");
    add<Pattern_Obelisk_Monocolor>(registry, "obelisk_mono");
    add<Pattern_Obelisk_Blobs>(registry, "obelisk_blobs");
    add<Pattern_Obelisk_RainbowBlobs>(registry, "obelisk_rainbow_blobs");
    add<Pattern_Obelisk_Prism>(registry, "obelisk_prism");

    // scanner
    add<scanner::Pattern_Scanner_PowerUp>(registry, "scanner_power_up");
    add<scanner::Pattern_Scanner_Boot>(registry, "scanner_boot");
    add<scanner::Pattern_Scanner_ScanIdle>(registry, "scanner_idle");
    add<scanner::Pattern_Scanner_Emergency>(registry, "scanner_emergency");
    add<scanner::Pattern_Scanner_DetectedWave>(registry, "scanner_detected");
    add<scanner::Pattern_Scanner_DetectedShimmer>(registry, "scanner_shimmer");
    add<scanner::Pattern_Scanner_DetectedMushroom>(registry, "scanner_mushroom");
    add<scanner::Pattern_Scanner_DetectedMushroomNew>(registry, "scanner_mushroom_new");
    add<scanner::Pattern_Scanner_SuccessMushroom>(registry, "scanner_success_mushroom");
    add<scanner::Pattern_Scanner_PlaybackMushroom>(registry, "scanner_playback_mushroom");
    add<scanner::Pattern_Scanner_PlaybackGeneric>(registry, "scanner_playback_generic");
    add<scanner::Pattern_Scanner_Loading>(registry, "scanner_loading");
    add<scanner::Pattern_Scanner_ArmFire>(registry, "scanner_arm_fire");
    add<scanner::Pattern_Scanner_SecretRain>(registry, "scanner_secret_rain");
    add<scanner::Pattern_Scanner_SecretAlien>(registry, "scanner_secret_alien");
    add<scanner::Pattern_Scanner_SecretForest>(registry, "scanner_secret_forest");

    // OSC mode's looks: another host's colours and audio, off the bus
    add<scanner::Pattern_Osc_Wash>(registry, "scanner_osc_wash");
    add<scanner::Pattern_Osc_Bloom>(registry, "scanner_osc_bloom");
    add<scanner::Pattern_Osc_Meter>(registry, "scanner_osc_meter");
    add<scanner::Pattern_Osc_Chase>(registry, "scanner_osc_chase");
    add<scanner::Pattern_Osc_Steps>(registry, "scanner_osc_steps");
    add<scanner::Pattern_Osc_Spectrum>(registry, "scanner_osc_spectrum");

    // the generic looks every rig can run
    add<scanner::Pattern_Generic_Rainbow>(registry, "generic_rainbow");
    add<scanner::Pattern_Generic_Chase>(registry, "generic_chase");
    add<scanner::Pattern_Generic_Fire2012>(registry, "generic_fire");
    add<scanner::Pattern_Generic_Flow>(registry, "generic_flow");
    add<scanner::Pattern_Generic_Lake>(registry, "generic_lake");
    add<scanner::Pattern_Generic_MatrixRain>(registry, "generic_matrix_rain");
    add<scanner::Pattern_Generic_Pacifica>(registry, "generic_pacifica");
    add<scanner::Pattern_Generic_Phased>(registry, "generic_phased");
    add<scanner::Pattern_Generic_Saw>(registry, "generic_saw");
    add<scanner::Pattern_Generic_SpotsFade>(registry, "generic_spots_fade");
    add<scanner::Pattern_Generic_TwinkleUp>(registry, "generic_twinkle_up");
    add<scanner::Pattern_Generic_Waterfall>(registry, "generic_waterfall");
    add<scanner::Pattern_Generic_ColorClouds>(registry, "generic_color_clouds");
    add<generic::Pattern_Audio_Meter>(registry, "audio_meter");

    // mythos26: the show's looks, and its solid as the rig's `solid`
    add<mythos26::Pattern_Mythos_Solid>(registry, "solid");
    add<mythos26::Pattern_Mythos_BeatPulse>(registry, "mythos_beat_pulse");
    add<mythos26::Pattern_Mythos_TvStatic>(registry, "mythos_tv_static");
    add<mythos26::Pattern_Mythos_House>(registry, "mythos_house");
    add<mythos26::Pattern_Mythos_NoiseWash>(registry, "mythos_noise_wash");
    add<mythos26::Pattern_Mythos_BusWash>(registry, "mythos_bus_wash");
    add<mythos26::Pattern_Mythos_KickColor>(registry, "mythos_kick_color");
    add<mythos26::Pattern_Mythos_CanyonWave>(registry, "mythos_canyon_wave");
    add<mythos26::Pattern_Mythos_GradientStrobe>(registry, "mythos_gradient_strobe");
    add<mythos26::Pattern_Mythos_Fire>(registry, "mythos_fire");
    add<mythos26::Pattern_Mythos_Rain>(registry, "mythos_rain");
    add<mythos26::Pattern_Mythos_Nova>(registry, "mythos_nova");
    add<mythos26::Pattern_Mythos_CloudFlight>(registry, "mythos_cloud_flight");
    add<mythos26::Pattern_Mythos_Reaction>(registry, "mythos_reaction");
    add<mythos26::Pattern_Mythos_Scaffold>(registry, "mythos_scaffold");
    add<mythos26::Pattern_Mythos_HueCycle>(registry, "mythos_hue_cycle");
}
