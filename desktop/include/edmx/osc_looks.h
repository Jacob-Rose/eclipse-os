// Copyright 2026 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "lib/ecore/hsv.h"

#include "relics/scanner/scanner_patterns.h"

#include "edmx/beat_clock.h"

///
/// The OSC mode's looks: afterglow's tower driven by another host.
///
/// A rock on the scanner hands the rig to whatever is sending OSC at the Pi
/// (see OscLiveState in afterglow's game.py), and a second rock steps through
/// these. They are the scanner machine's states like any other, so the game
/// names them with `state <tag> <seconds>` and the blend between two is the
/// machine's own.
///
/// Desk-only, unlike the rest of the scanner's looks: they read the audio
/// bus, which exists on the desk and not on a relic. On the tower that costs
/// nothing - the Pi renders the ring and the obelisk both, and streams the
/// obelisk its picture.
///
/// What they read, all of it filled by the OSC map (data/osc-map.json in
/// afterglow) and none of it named for any one sending app:
///
///   bus   level_instant   peaks on every kick - the flash
///         level_average   the level against a short window - the body
///         level_meter     the quantised meter bar - the height
///         beat            spikes on the beat - a rising edge is a beat
///         bpm             50..220 scaled to 0..1 - how long a beat takes
///   knob  hue_a sat_a     the primary colour
///         hue_b sat_b     the secondary
///
/// The colours are knobs rather than bus channels because they are not
/// levels: they hold where they were put, and a host that stops sending
/// leaves the last colour up rather than fading it to black. And they are
/// one pair for all three looks - every look's knobs write the same two
/// colours - so the golden key stepping to the next look keeps the host's
/// colours rather than coming up on its own. The looks have none of their
/// own: until the host says otherwise both are white, and the looks are
/// shapes and movement, not palettes.
///
namespace edmx
{
    /// The host's two colours, shared by every OSC look.
    struct OscColours
    {
        float hueA{0.0f};
        float satA{0.0f};
        float hueB{0.0f};
        float satB{0.0f};
    };

    OscColours& sharedOscColours();

    /// What every OSC look shares: the colours, the reading of the bus, and
    /// a beat clock off the sender's grid.
    class Pattern_Osc_Look : public scanner::PatternScanner
    {
    public:
        /// How hard the levels drive the look.
        float gain{1.0f};
        /// The least the look shows with nothing arriving, so the mode is
        /// visibly on before the host is.
        float floorLevel{0.15f};
        /// Seconds the average and the meter glide over; the instant does not.
        float slew{0.08f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    protected:
        ecore::HSV primary(float value) const { return ecore::HSV(colours.hueA, colours.satA, value); }
        ecore::HSV secondary(float value) const { return ecore::HSV(colours.hueB, colours.satB, value); }

        OscColours& colours{sharedOscColours()};

        /// How far through the current beat, 0..1, held at 1 once it is over
        /// and nothing new has come.
        float beatPhase() const;

        AudioLevel* bus{nullptr};
        float instant{0.0f};
        float average{0.0f};
        float meter{0.0f};
        float bpm{120.0f};
        /// Seconds since the last rising edge on `beat`, large before the first.
        float sinceBeat{1000.0f};
        /// Beats counted since entry, for looks that alternate.
        int beats{0};

    private:
        bool beatHigh{false};
    };

    /// osc_wash: the stage a gradient from primary at the foot to secondary
    /// at the top, its midpoint rolling up one stage every four beats; bright
    /// on the average, flashing toward white-hot on the instant.
    class Pattern_Osc_Wash : public Pattern_Osc_Look
    {
    public:
        /// How far toward full an instant peak lifts the whole stage.
        float flash{0.6f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float roll{0.0f};
    };

    /// osc_pulse: the secondary as a bed under the average, and on every
    /// beat a ring of primary leaving the origin line both ways, gone by the
    /// next beat.
    class Pattern_Osc_Pulse : public Pattern_Osc_Look
    {
    public:
        /// The ring's width, a share of the stage.
        float ring{0.1f};
        /// How bright the bed sits, against the average.
        float bed{0.4f};

        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };

    /// osc_meter: the meter as a bar up the stage, primary at the foot to
    /// secondary at its head, with a peak cap that falls slowly and flashes
    /// on the beat.
    class Pattern_Osc_Meter : public Pattern_Osc_Look
    {
    public:
        /// Stage heights a second the peak cap falls at.
        float fall{0.35f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float peak{0.0f};
    };
}
