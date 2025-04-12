// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "state_sleep.h"

#include "../../../imgs/matrix.h"


State_Sleep::State_Sleep(const char* InStateName,  RelicIO* inIO) : State_PendantGeneric(InStateName, inIO)
{

}

void State_Sleep::init()
{
    State_PendantGeneric::init();

    setGenerator(std::make_shared<Pattern_Sleep>());

    setStateStartGifData((uint8_t *)matrix, sizeof(matrix));
}