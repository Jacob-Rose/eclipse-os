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
/// these. Two kinds: osc_wash, osc_bloom and osc_meter are *presence*
/// looks, written for a jam session rather than a bounced song - they move
/// on the bands' slow presence and never flash on a beat or a kick - and
/// osc_chase and osc_steps, last in the game's list, are the beat looks,
/// which lock to the sender's tempo and flash on it. Every one of them runs
/// held back (`calm`) until the beat kicks in - a run of steady beats from
/// the sender (`groove`) - and settles back when it is lost.
///
/// They are the scanner machine's states like any other, so the game
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
///         sub bass        the bands, each a layer of its own in every
///         mid high        look; read twice, as it is (fast) and as its
///                         presence (slow), and bass against high is the
///                         timbre
///         bass_hits       the kick, jumping to 1 and decaying - the beat
///                         looks' punch
///         hits            any sharp hit - unused, kept on the bus
///         beat            spikes on the beat - a rising edge is a beat,
///                         and a run of them is the groove
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
        /// How hard the look runs before the beat kicks in, against full
        /// once it has: what the music drives (not the floor) is scaled
        /// from this to 1 as the groove comes up.
        float calm{0.35f};
        /// Seconds a band's presence glides over: the slow reading the
        /// presence looks move on, under the bands' fast one.
        float presenceSlew{0.6f};

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

        /// What the music drives the look with, calm..1 with the groove.
        float drive() const { return calm + (1.0f - calm) * groove; }

        /// Which beat of the bar this is, 0..3, off the host's tick; counted
        /// from entry when no tick is arriving.
        int barBeat() const;

        /// Where a node stands around the obelisk, 0..1, a closed loop:
        /// its column over the eight. The ring's nodes answer by their x
        /// across the same width, which puts them in step with the faces.
        static float around(const eio::HSVStripNode* node);

        AudioLevel* bus{nullptr};
        float level{0.0f};
        float sub{0.0f};
        float bass{0.0f};
        float mid{0.0f};
        float high{0.0f};
        /// The same, slow: how much of each band is there at all.
        float levelPresence{0.0f};
        float subPresence{0.0f};
        float bassPresence{0.0f};
        float midPresence{0.0f};
        float highPresence{0.0f};
        /// 0..1: up over a couple of seconds once the sender's beats arrive
        /// steadily, down over a few once they stop.
        float groove{0.0f};
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
        /// Beats in a row, each within reach of the last.
        int streak{0};
        float lastTick{-1.0f};
        /// Seconds since the tick knob last changed, large before the first.
        float sinceTick{1000.0f};
    };

    /// osc_wash: a tide. The stage is a gradient from primary to secondary,
    /// each colour taking as much of it as it takes of the host's screen,
    /// rolling up it slowly and hurrying with the mids' presence. Four
    /// layers under it: the sub a deep swell rising from the floor, the bass
    /// the body of the gradient, the mids a slow wave of brightness drifting
    /// around the faces, the highs a soft shimmer at the top. No beat in it.
    class Pattern_Osc_Wash : public Pattern_Osc_Look
    {
    public:
        /// How far up the stage the sub's swell reaches at full.
        float swell{0.45f};
        /// How deep the highs' shimmer is.
        float glitter{0.35f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float roll{0.0f};
        float drift{0.0f};
        float time{0.0f};
    };

    /// osc_bloom: an aura on the origin line, where the ring sits. The bass
    /// is its core, primary, reaching up and down the stage with the bass's
    /// presence; the mids a shell of secondary around it, turning slowly
    /// round the faces in lobes; the sub a dim halo over the whole stage;
    /// the highs fine threads climbing out of the core's edge. It breathes
    /// with the presence and nothing else.
    class Pattern_Osc_Bloom : public Pattern_Osc_Look
    {
    public:
        /// How far the core reaches each way at full bass, a share of the
        /// stage.
        float reach{0.5f};
        /// How bright the mids' shell runs.
        float shell{0.7f};
        /// Turns a second the shell's lobes make, at full mids.
        float spin{0.12f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float turn{0.0f};
        float climb{0.0f};
    };

    /// osc_meter: the bands as meters, one to a face of the obelisk - sub,
    /// bass, mid, high going round - each a bar of its presence up the stage
    /// from primary (the sub) to secondary (the highs), with a cap on its
    /// recent peak that falls slowly. The ring carries the whole level, lit
    /// round from its foot. Steady: no beat, no kick.
    class Pattern_Osc_Meter : public Pattern_Osc_Look
    {
    public:
        /// Stage heights a second a cap falls at.
        float fall{0.25f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float peaks[4]{0.0f, 0.0f, 0.0f, 0.0f};
    };

    /// osc_chase: on the beat. Each beat a band of primary leaves the origin
    /// line and climbs to the top as the beat runs out, twisting round the
    /// faces - the other way on alternate beats - with the stage flashing as
    /// it leaves, hardest on the one. Under it the sub and bass are a bed at
    /// the foot, the highs a trail behind the band, and the kick punches the
    /// band itself. Before the beat kicks in it is the bed and nothing more.
    class Pattern_Osc_Chase : public Pattern_Osc_Look
    {
    public:
        /// How far toward full the beat flashes the stage.
        float flash{0.6f};
        /// The band's width, a share of the stage.
        float width{0.12f};
        /// Turns round the faces the band makes on its way up.
        float twist{0.5f};

        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;
    };

    /// osc_steps: the bar in four steps up the stage. Beat one lights the
    /// foot, two the next quarter, then three, then the top, each in the
    /// colour opposite the last and fading through its beat, and the one
    /// flashes the whole stage. The bass sets how bright a step lands, the
    /// mids how much of the last step lingers, the highs glint on the lit
    /// step, and the sub glows under all of it.
    class Pattern_Osc_Steps : public Pattern_Osc_Look
    {
    public:
        /// How far toward full the one flashes the whole stage.
        float flash{0.7f};
        /// How much of the beat a step takes to fade, 0..1.
        float decay{0.8f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float time{0.0f};
    };
}
