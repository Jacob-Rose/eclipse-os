// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "eclipse_test.h"

#include <emidi/beat_notes.h>

using namespace emidi;

TEST(beat_notes_decode_mixxx_by_default)
{
    BeatNotes notes;

    const BeatNotes::Event beat = notes.decode(Message(0x90, 50, 100));
    CHECK(beat.kind == BeatNotes::Kind::Beat);

    const BeatNotes::Event tempo = notes.decode(Message(0x90, 52, 78));
    CHECK(tempo.kind == BeatNotes::Kind::Tempo);
    CHECK_NEAR(tempo.bpm, 128.0f, 1e-6f);

    const BeatNotes::Event meter = notes.decode(Message(0x95, 69, 127));   // any channel
    CHECK(meter.kind == BeatNotes::Kind::Level);
    CHECK_EQ(meter.level, 2);
    CHECK_NEAR(meter.value, 1.0f, 1e-6f);

    CHECK(notes.decode(Message(0x90, 48, 101)).kind == BeatNotes::Kind::None);   // a deck change
    CHECK(notes.decode(Message(0x80, 50, 0)).kind == BeatNotes::Kind::None);     // a note off
    CHECK(notes.decode(Message(0x90, 50, 0)).kind == BeatNotes::Kind::None);     // also a note off
    CHECK(notes.decode(Message(0xB0, 50, 100)).kind == BeatNotes::Kind::None);   // a controller
    CHECK(notes.claims(Message(0x90, 64, 10)));
    CHECK(!notes.claims(Message(0x90, 12, 10)));
}

TEST(beat_notes_are_all_renumberable)
{
    BeatNotes notes;
    notes.setChannel(2);
    notes.setBeatNote(-1);      // any note that is nothing else
    notes.setTempoNote(-1);
    notes.setLevelNote(0, -1);
    notes.setLevelNote(1, 36);
    notes.setLevelNote(9, 1);   // no such slot: ignored
    notes.setTempoOffset(0.0f);

    CHECK(notes.decode(Message(0x90, 50, 100)).kind == BeatNotes::Kind::None);   // channel 1
    CHECK(notes.decode(Message(0x91, 50, 100)).kind == BeatNotes::Kind::Beat);
    CHECK(notes.decode(Message(0x91, 52, 100)).kind == BeatNotes::Kind::Beat);   // no tempo note now
    CHECK(notes.decode(Message(0x91, 64, 100)).kind == BeatNotes::Kind::Beat);   // slot 0 is off
    const BeatNotes::Event level = notes.decode(Message(0x91, 36, 64));
    CHECK(level.kind == BeatNotes::Kind::Level);
    CHECK_EQ(level.level, 1);
    CHECK_EQ(notes.getLevelNote(9), -1);
    CHECK_EQ(notes.getChannel(), 2);
    notes.setChannel(99);
    CHECK_EQ(notes.getChannel(), -1);
}
