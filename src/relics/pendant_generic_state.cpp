// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "pendant_generic_state.h"

void State_PendantGeneric::init() 
{
    State::init();
    if (io)
    {
        io->init();
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