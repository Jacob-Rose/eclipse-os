// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <atomic>
#include <string>

namespace ebeat
{
    /// The three loudness signals a DJ app sends as notes: instantaneous,
    /// the level against its recent window, and a quantised meter bar.
    enum class VuSource
    {
        Instant,
        Average,
        Meter,
    };
    constexpr int kVuSourceCount = 3;
    const char* describeVuSource(VuSource source);

    /// One channel of the audio bus. The first block is what an analyser
    /// publishes (Synesthesia's syn_* uniforms, by name); the last three are
    /// the VU notes. A look reads a channel by name and never learns which
    /// cable filled it.
    enum class AudioChannel
    {
        Level,          ///< the whole spectrum
        Sub,            ///< below the bass: a mic's sub band
        Bass,
        Mid,
        MidHigh,
        High,
        Hits,           ///< transients
        BassHits,
        MidHits,
        MidHighHits,
        HighHits,
        Presence,
        BassPresence,
        MidPresence,
        MidHighPresence,
        HighPresence,
        Beat,           ///< spikes on the detected beat
        Bpm,            ///< the tempo, scaled across 50..220
        BpmConfidence,
        Intensity,      ///< accumulated song intensity
        LevelInstant,   ///< VU: peaks on every kick
        LevelAverage,   ///< VU: the level against its two-second window
        LevelMeter,     ///< VU: the quantised meter bar
        Count           ///< not a channel; the size of the bus
    };
    constexpr int kAudioChannelCount = static_cast<int>(AudioChannel::Count);

    /// `level`, `bass_hits`, `level_meter`...
    const char* audioChannelName(AudioChannel channel);
    bool findAudioChannel(const std::string& name, AudioChannel& outChannel);

    constexpr AudioChannel channelFor(VuSource source)
    {
        return (source == VuSource::Instant) ? AudioChannel::LevelInstant
             : (source == VuSource::Meter)   ? AudioChannel::LevelMeter
             :                                 AudioChannel::LevelAverage;
    }

    /* @brief The audio bus: how loud the music is, per channel, right now.
    *
    * Something upstream (a MIDI VU note, an OSC analyser) sets a channel with
    * the time it arrived; a look gets it every frame. A channel holds its
    * value for kHoldFor, then fades to nothing by kStaleAfter, so a dropped
    * link goes dark instead of sticking at whatever the music was doing.
    * Every field is atomic: one thread writes, another reads.
    *
    *     bus.set(AudioChannel::Bass, 0.7f, when);
    *     const float bass = bus.get(AudioChannel::Bass, now);
    */
    class AudioLevel
    {
    public:
        /// Seconds a reading is current, then seconds until it has faded out.
        static constexpr double kHoldFor = 0.35;
        static constexpr double kStaleAfter = 0.8;

        void set(AudioChannel channel, float level, double when);
        float get(AudioChannel channel, double now) const;
        /// Fed within kHoldFor.
        bool isLive(AudioChannel channel, double now) const;
        unsigned long long getUpdates(AudioChannel channel) const;

        void set(VuSource source, float level, double when) { set(channelFor(source), level, when); }
        float get(VuSource source, double now) const { return get(channelFor(source), now); }
        bool isLive(VuSource source, double now) const { return isLive(channelFor(source), now); }
        unsigned long long getUpdates(VuSource source) const { return getUpdates(channelFor(source)); }

        bool isAnyLive(double now) const;

        /// `avg=0.40 inst=0.72 meter=0.50`
        std::string describe(double now) const;
        /// Every live channel, `bass=0.70 hits=0.10`, or `none`.
        std::string describeLive(double now) const;

    private:
        struct Reading
        {
            std::atomic<float> level{0.0f};
            std::atomic<double> stamp{-1.0};
            std::atomic<unsigned long long> updates{0};
        };
        Reading readings[kAudioChannelCount];
    };
}
