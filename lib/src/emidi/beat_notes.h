// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <atomic>

#include "message.h"

namespace emidi
{
    /* @brief Beats carried by notes, the way DJ software sends them: a note on
    * each beat, a note whose velocity is the tempo, and a few whose velocity is
    * a level meter.
    *
    * The defaults are Mixxx's "MIDI for light" mapping: note 50 is the beat,
    * 52 the tempo as velocity + 50, and 64, 68, 69 the instantaneous, average
    * and meter levels. Every number is settable, and settable from another
    * thread than the one decoding.
    *
    *     const BeatNotes::Event event = notes.decode(message);
    *     switch (event.kind)
    *     {
    *         case BeatNotes::Kind::Beat:  clock.markBeat(when, ebeat::BeatSource::MidiNote); break;
    *         case BeatNotes::Kind::Tempo: clock.setBpm(event.bpm, ebeat::BeatSource::MidiNote, when); break;
    *         case BeatNotes::Kind::Level: levels[event.level] = event.value; break;
    *         default: break;
    *     }
    *
    * @see ClockFollower for the 0xF8 way of saying the same thing
    * @see ebeat::BeatClock for where the beats go
    */
    class BeatNotes
    {
    public:
        static constexpr int kLevelSlots = 3;

        enum class Kind { None, Beat, Tempo, Level };

        struct Event
        {
            Kind kind{Kind::None};
            float bpm{0.0f};     ///< Tempo
            int level{-1};       ///< Level: which slot, 0..kLevelSlots-1
            float value{0.0f};   ///< Level: 0..1
        };

        /// Only notes on this channel (1..16) count; -1 takes any.
        void setChannel(int inChannel) { channel.store((inChannel >= 1 && inChannel <= 16) ? inChannel : -1); }
        int getChannel() const { return channel.load(); }

        /// The note that is a beat; -1 takes any note on that is nothing else.
        void setBeatNote(int note) { beatNote.store(note); }
        int getBeatNote() const { return beatNote.load(); }

        /// The note whose velocity carries the tempo; -1 ignores it.
        void setTempoNote(int note) { tempoNote.store(note); }
        int getTempoNote() const { return tempoNote.load(); }
        /// bpm = velocity + offset.
        void setTempoOffset(float offset) { tempoOffset.store(offset); }
        float getTempoOffset() const { return tempoOffset.load(); }

        /// The note whose velocity is level `slot`; -1 ignores it.
        void setLevelNote(int slot, int note);
        int getLevelNote(int slot) const;

        /// What a message means, or None for anything that is not a note on
        /// this decoder takes.
        Event decode(const Message& message) const;

        /// Whether a message is one decode() would take, without decoding it.
        bool claims(const Message& message) const { return decode(message).kind != Kind::None; }

    private:
        std::atomic<int> channel{-1};
        std::atomic<int> beatNote{50};
        std::atomic<int> tempoNote{52};
        std::atomic<float> tempoOffset{50.0f};
        std::atomic<int> levelNotes[kLevelSlots]{{64}, {68}, {69}};
    };
}
