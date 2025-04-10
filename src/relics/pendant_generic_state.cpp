// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "pendant_generic_state.h"


void State_PendantGeneric::onStateChangeState(StateStatus inStatus)
{
    State_GenericHSV::onStateChangeState(inStatus);

    if(inStatus == StateStatus::Active)
    {
        if (io && io->getScreenDrawer() && gifData && !bHasSetGif)
        {
            io->getScreenDrawer()->cancelGifRender();
            io->getScreenDrawer()->setScreenGif(gifData, gifDataSize);
        }
    }
    if(inStatus == StateStatus::Off)
    {
        if (bHasSetGif)
        {
            bHasSetGif = false;
        }
    }
}

void State_PendantGeneric::tick(float deltaTime)
{
    State_GenericHSV::tick(deltaTime);
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
