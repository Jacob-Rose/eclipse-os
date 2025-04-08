// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "pendant_generic_state.h"


void State_PendantGeneric::onStateChangeState(StateStatus inStatus)
{
    if(inStatus == StateStatus::TransitionIn)
    {
        if (io && gifData && gifDataSize > 0)
        {
            io->getScreenDrawer()->setScreenGif(gifData, gifDataSize);
        }
    }
}

void State_PendantGeneric::tick(float deltaTime)
{
    State::tick(deltaTime);
    if (io)
    {
        io->tick(deltaTime);
    }
}

void State_PendantGeneric::setGifData(uint8_t *inGifData, int inGifDataSize)
{
    gifData = inGifData;
    gifDataSize = inGifDataSize;
}
