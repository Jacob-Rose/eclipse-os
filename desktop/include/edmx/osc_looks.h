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
/// osc_chase and osc_steps, last in the game's list, are the beat looks.
/// They move on the *kick* the mic hears, never on the sender's beat clock:
/// that is guessed from the music and drifts on a jam, and the kick is the
/// drummer. Each kick swells in over `attack` and dies over `release`,
/// rather than strobing. The sender's tempo is used once, as a speed:
/// osc_chase's pole scrolls a stripe a beat, running free. Every look runs held back (`calm`)
/// until the beat kicks in - a run of steady kicks (`groove`) - and settles
/// back when it is lost.
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
///         bass_hits       the kick, jumping to 1 and decaying - its rising
///                         edge is a hit when no `kick` trigger has just
///                         been one, and a run of hits is the groove
///         hits            any sharp hit - unused, kept on the bus
///         bpm             50..220 scaled to 0..1 - how fast osc_chase's
///                         pole scrolls, and nothing else
///         beat            spikes on the sender's beat - unused: its clock
///                         is a guess and drifts on a jam
///   knob  hue_a sat_a     the primary colour
///         hue_b sat_b     the secondary
///         bright_a/_b     how bright each colour is on the host's screen
///         amount_a/_b     how much of the screen each covers
///         tick            the host's beat count - unused, kept as a knob
///         cut             set to 1 on each new video; the look takes it
///         kick            set to 1 on each kick, the moment the host hears
///                         it; the look takes it - the beat looks' timing
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
        /// 1 from the host on each kick, the moment it is heard, back to 0
        /// once a look has seen it - the kick to time by, where the decaying
        /// bass_hits level is only sampled 30 times a second.
        float kick{0.0f};
        /// When the last kick trigger was seen, in nowSeconds(); shared like
        /// cutAt.
        double kickAt{-1000.0};
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

        /// How far from the last kick to the next one due, 0..1, by the gap
        /// between the last few.
        float hitPhase() const;

        /// The last hit as a swell: up over `attack` seconds, then down over
        /// `release`; 0 before the first.
        float hitEnvelope(float attack, float release) const;

        /// A brightness through the host's screen and the last cut: dimmed
        /// as the screen goes dark (follow), lifted as a new video lands.
        float finish(float value) const;

        /// The primary's share of the two colours on the host's screen, 0..1,
        /// a half when it says nothing.
        float balance() const;

        /// What the music drives the look with, calm..1 with the groove.
        float drive() const { return calm + (1.0f - calm) * groove; }

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
        /// 0..1: up over a couple of seconds once the kicks arrive
        /// steadily, down over a few once they stop.
        float groove{0.0f};
        /// 0..1: a voice or a piano coming in - the mids' presence risen
        /// over where it has sat for the last several seconds, swelling in
        /// and easing out. It settles back as the new level becomes the room.
        float midEntry{0.0f};
        float kick{0.0f};
        float onset{0.0f};
        /// High against bass, 0..1: a half for a level mix, toward 1 for
        /// hats and cymbals, toward 0 for a bassline.
        float timbre{0.5f};
        float bpm{120.0f};
        /// Seconds since the last kick, large before the first.
        float sinceHit{1000.0f};
        /// The gap between kicks, smoothed over the last few.
        float hitGap{0.5f};
        /// Kicks counted since entry, for looks that alternate.
        int hits{0};
        /// Whether a kick landed this frame.
        bool hitNow{false};
        /// 1 as a new video lands, falling to 0 over cutSeconds.
        float cutGlow{0.0f};

    private:
        float midBaseline{0.0f};
        bool kickHigh{false};
        /// The last kick trigger this look has taken.
        double kickSeen{-1000.0};
        /// Hits in a row, each within reach of the last.
        int streak{0};
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

    /// osc_bloom: an aura on the origin line, where the ring sits, in light
    /// rather than the host's colours: red is the bass and the sub, green
    /// the mids, blue the highs, each its own layer and each eased in over
    /// `attack` and out over `release`, so the colour glides between them
    /// rather than flashing. Red is the core, reaching up and down the stage
    /// with the bass and glowing dim over all of it with the sub; green a
    /// shell around it, turning slowly round the faces in lobes; blue fine
    /// threads climbing out of the core and a haze up the tower. Where they
    /// overlap they mix as light does - a voice over a bassline is yellow.
    /// The pars are all three at once, on the bands' presence rather than
    /// their level and eased slower still - the room's colour, breathing -
    /// lifting as a voice or a piano comes in.
    class Pattern_Osc_Bloom : public Pattern_Osc_Look
    {
    public:
        /// How far the core reaches each way at full bass, a share of the
        /// stage.
        float reach{0.5f};
        /// How bright the mids' shell runs.
        float shell{0.8f};
        /// Turns a second the shell's lobes make, at full mids.
        float spin{0.12f};
        /// Seconds each colour takes to come up with its band, and to fall
        /// away after it.
        float attack{0.08f};
        float release{0.5f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float turn{0.0f};
        float climb{0.0f};
        float red{0.0f};
        float green{0.0f};
        float blue{0.0f};
        float parRed{0.0f};
        float parGreen{0.0f};
        float parBlue{0.0f};
    };

    /// osc_meter: the bands as meters, one to a face of the obelisk - sub,
    /// bass, mid, high going round - each a bar of its presence up the stage
    /// in the rainbow, red at the foot to violet at the top, each face a
    /// quarter of the wheel on from the last and the whole wheel drifting
    /// slowly, with a pale cap on its recent peak that falls slowly. The
    /// ring carries the whole level, lit round from its foot. The pars are a
    /// meter too, mirrored out from the middle of the truss - the sub's
    /// pair in the centre, then the bass, the mids, the highs, the level on
    /// the ends - each pair its band's colour and as bright as the band's
    /// presence, eased slowly over `attack` and `release`, all of them
    /// lifting as a voice or a piano comes in. Steady: no beat, no kick.
    class Pattern_Osc_Meter : public Pattern_Osc_Look
    {
    public:
        /// Stage heights a second a cap falls at.
        float fall{0.25f};
        /// Seconds the pars take to come up with their band, and to fall.
        float attack{0.4f};
        float release{1.5f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float peaks[4]{0.0f, 0.0f, 0.0f, 0.0f};
        /// The pars' bands, eased: sub, bass, mid, high, level.
        float pars[5]{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        float hueDrift{0.0f};
    };

    /// osc_chase: a barber's pole, or a theatre marquee, kept low: a few
    /// narrow stripes of lamps up the stage, primary and secondary in turn
    /// with wide dark gaps, wound round the faces and climbing a stripe every
    /// eight beats on the sender's tempo - a slow crawl, run free, never a
    /// flash or a jump on a beat. A kick sends a soft, slow pulse up the
    /// pole from the foot, the lamps warming as it passes them; a voice or a piano
    /// coming in fattens the lamps and fills them in. The sub and bass glow
    /// at the foot between them.
    class Pattern_Osc_Chase : public Pattern_Osc_Look
    {
    public:
        /// Stripes up the stage, one of each colour to a pair.
        float stripes{4.0f};
        /// Times a stripe winds round the faces on its way up.
        float turns{1.0f};
        /// Beats a stripe takes to pass a point.
        float beatsPerStripe{8.0f};
        /// How bright a kick's pulse lifts the lamps it passes.
        float pulse{0.3f};
        /// Seconds a kick's pulse takes to come up, and to fade.
        float attack{0.15f};
        float release{0.8f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

        /// Seconds a kick's pulse takes to climb the stage.
        float climb{3.0f};

    private:
        float scroll{0.0f};
        /// Each kick's pulse, by its age in seconds; the oldest is replaced.
        static constexpr int kPulses = 8;
        float pulseAges[kPulses]{1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f};
        int nextPulse{0};
    };

    /// osc_steps: four steps up the stage. Each kick lights the next quarter -
    /// the foot, then up, then the top, then the foot again - primary and
    /// secondary in turn over a ground that is always the secondary,
    /// swelling in and easing out, and breathes the whole stage a little
    /// with it, the foot's step most. A voice or a piano coming in rises up
    /// the stage from the foot as a glow. The bass sets how bright a step comes up, the mids how much of
    /// the last one lingers, the highs glint on the lit step, and the sub
    /// glows under all of it.
    class Pattern_Osc_Steps : public Pattern_Osc_Look
    {
    public:
        /// How far toward full a kick breathes the whole stage; the foot's
        /// step twice as far.
        float flash{0.12f};
        /// Seconds a step takes to come up, and to die away.
        float attack{0.06f};
        float release{0.45f};

        virtual void reset() override;
        virtual void tick(float deltaTime) override;
        virtual void render(eio::HSVStripNode* inNode, ecore::HSV& inOutColor) const override;
        virtual void reflect(ecore::PropertyBag& bag) override;

    private:
        float time{0.0f};
        int step{3};
    };
}
