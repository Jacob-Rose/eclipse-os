// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "beat_notes.h"

using namespace emidi;

void BeatNotes::setLevelNote(int slot, int note)
{
    if (slot >= 0 && slot < kLevelSlots)
    {
        levelNotes[slot].store(note);
    }
}

int BeatNotes::getLevelNote(int slot) const
{
    return (slot >= 0 && slot < kLevelSlots) ? levelNotes[slot].load() : -1;
}

BeatNotes::Event BeatNotes::decode(const Message& message) const
{
    Event event;
    if (!message.isNoteOn())
    {
        return event;
    }
    const int wantedChannel = channel.load();
    if (wantedChannel >= 0 && message.channel() != wantedChannel)
    {
        return event;
    }

    const int note = message.number();
    const int tempo = tempoNote.load();
    if (tempo >= 0 && note == tempo)
    {
        event.kind = Kind::Tempo;
        event.bpm = static_cast<float>(message.value()) + tempoOffset.load();
        return event;
    }
    for (int slot = 0; slot < kLevelSlots; ++slot)
    {
        const int levelNote = levelNotes[slot].load();
        if (levelNote >= 0 && note == levelNote)
        {
            event.kind = Kind::Level;
            event.level = slot;
            event.value = static_cast<float>(message.value()) / 127.0f;
            return event;
        }
    }
    const int beat = beatNote.load();
    if (beat < 0 || note == beat)
    {
        event.kind = Kind::Beat;
    }
    return event;
}
