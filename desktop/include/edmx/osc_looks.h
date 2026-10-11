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
/// What they read, all of it filled by the OSC map (config/oscmaps/
/// afterglow.json, which the game and config/afterglow_osc.json both open).
/// The map is written for sync swim (the TV visualiser at Mythos), whose
/// addresses the test host in ~/Documents/osc-host sends as well; the looks
/// only know the channels and knobs:
///
///   bus   level           overall loudness - the body
///         bass mid high   the bands - the bass widens, the mids hurry,
///                         the highs glitter, and bass against high is the
///                         timbre
///         bass_hits       the kick, jumping to 1 and decaying - the flash
///         hits            any sharp hit - a flick on the bed
///         beat            spikes on the beat - a rising edge is a beat
///         bpm             50..220 scaled to 0..1 - how long a beat takes
///   knob  hue_a sat_a     the primary colour
///         hue_b sat_b     the secondary
///         bright_a/_b     how bright each colour is on the host's screen
///         amount_a/_b     how much of the screen each covers
///         tick            the host's beat count - which beat is the one
///         cut             set to 1 on each new video; the look takes it
///
/// The bus is for levels, which fade out when the host goes quiet. The rest
/// are knobs because they are not levels: they hold where they were put, so
/// a host that stops sending leaves its last colour up rather than fading it
/// to black. They are one set for all three looks - every look's knobs write
/// the same OscHost - so the golden key stepping to the next look keeps the
/// host's colours rather than coming up on its own. The looks have no
/// colours of their own: until the host says otherwise both are white at
/// full brightness, and the looks are shapes and movement, not palettes.
///
namespace edmx
{
    /// The host's knobs, shared by every OSC look.
    struct OscHost
    {
        float hueA{0.0f};
        float satA{0.0f};
        float hueB{0.0f};
        float satB{0.0f};
        float brightA{1.0f};
        float brightB{1.0f};
        float amountA{0.0f};
        float amountB{0.0f};
        /// The host's beat count; beat n is the one when n % 4 is 0.
        float tick{0.0f};
        /// 1 from the host on a new video, back to 0 once a look has seen it.
        float cut{0.0f};
        /// When the last cut was seen, in nowSeconds(); shared so that two
        /// looks in a blend light it together.
        double cutAt{-1000.0};
    };

    OscHost& sharedOscHost();

    /// What every OSC look shares: the host's knobs, the reading of the bus,
    /// and a beat clock off the sender's grid.
    class Pattern_Osc_Look : public scanner::PatternScanner
    {
    public:
        /// How hard the levels drive the look.
        float gain{1.0f};
        /// The least the look shows with nothing arriving, so the mode is
        /// visibly on before the host is.
        float floorLevel{0.15f};
        /// Seconds the level and the bands glide over; the hits do not.
        float slew{0.08f};
        /// How far the host's screen going dark (a break, a black frame)
        /// takes the rig down with it: 0 ignores it, 1 follows it to black.
        float follow{0.6f};
        /// How far a new video lifts the whole stage, fading over cutSeconds.
        float cutLift{0.7f};
        float cutSeconds{0.8f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    protected:
        ecore::HSV primary(float value) const { return ecore::HSV(host.hueA, host.satA, value); }
        ecore::HSV secondary(float value) const { return ecore::HSV(host.hueB, host.satB, value); }

        OscHost& host{sharedOscHost()};

        /// How far through the current beat, 0..1, held at 1 once it is over
        /// and nothing new has come.
        float beatPhase() const;

        /// A brightness through the host's screen and the last cut: dimmed
        /// as the screen goes dark (follow), lifted as a new video lands.
        float finish(float value) const;

        /// The primary's share of the two colours on the host's screen, 0..1,
        /// a half when it says nothing.
        float balance() const;

        /// How bright this beat's accent is: full on the one and under it on
        /// the rest, or full on every beat with no tick arriving.
        float accent() const;

        AudioLevel* bus{nullptr};
        float level{0.0f};
        float bass{0.0f};
        float mid{0.0f};
        float high{0.0f};
        float kick{0.0f};
        float onset{0.0f};
        /// High against bass, 0..1: a half for a level mix, toward 1 for
        /// hats and cymbals, toward 0 for a bassline.
        float timbre{0.5f};
        float bpm{120.0f};
        /// Seconds since the last rising edge on `beat`, large before the first.
        float sinceBeat{1000.0f};
        /// Beats counted since entry, for looks that alternate.
        int beats{0};
        /// Whether the beat now sounding is the one, off the host's tick;
        /// false with no tick arriving.
        bool downbeat{false};
        /// 1 as a new video lands, falling to 0 over cutSeconds.
        float cutGlow{0.0f};

    private:
        bool beatHigh{false};
        float lastTick{-1.0f};
        /// Seconds since the tick knob last changed, large before the first.
        float sinceTick{1000.0f};
    };

    /// osc_wash: the stage a gradient from primary to secondary rolling up
    /// about a stage a bar (faster on the mids), each colour taking as much of it as it takes of the
    /// host's screen; bright on the level, flashing on the kick, glittering
    /// on the highs.
    class Pattern_Osc_Wash : public Pattern_Osc_Look
    {
    public:
        /// How far toward full a kick lifts the whole stage.
        float flash{0.6f};
        /// How deep the highs' glitter cuts into the wash.
        float glitter{0.35f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float roll{0.0f};
        float time{0.0f};
    };

    /// osc_pulse: the secondary as a bed under the level, flicking on every
    /// hit, and on every beat a ring of primary leaving the origin line both
    /// ways, gone by the next beat - wider on the bass, brightest on the one.
    class Pattern_Osc_Pulse : public Pattern_Osc_Look
    {
    public:
        /// The ring's width, a share of the stage.
        float ring{0.1f};
        /// How bright the bed sits, against the level.
        float bed{0.4f};
        /// How far a hit flicks the bed up.
        float flick{0.25f};

        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };

    /// osc_meter: the level as a bar up the stage, primary at the foot to
    /// secondary at its head - the head further toward secondary the brighter
    /// the sound - punching on the kick, with a peak cap that falls slowly
    /// and flashes on the beat.
    class Pattern_Osc_Meter : public Pattern_Osc_Look
    {
    public:
        /// Stage heights a second the peak cap falls at.
        float fall{0.35f};
        /// How much of the bar's brightness is the kick's: 0 is a steady bar.
        float punch{0.25f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float peak{0.0f};
    };
}
