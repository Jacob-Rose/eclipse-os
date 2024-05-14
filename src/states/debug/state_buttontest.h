// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../state_base.h"

#include "../../lib/j/jcolors.h"


class State_ButtonTest : public State
{
public:
    State_ButtonTest(const char* InStateName);

    virtual void tick() override;

    j::HSV GreenColor = j::HSV(0.2f, 1.f,0.5f);
    j::HSV RedColor =   j::HSV(0.f, 1.f, 0.5f);
    j::HSV BlueColor =  j::HSV(0.5f, 1.f, 0.5f);
    j::HSV WhiteColor = j::HSV(0.f, 0.f, 0.5f);
    j::HSV BlackColor = j::HSV(0.8f, 0.5f, 0.5f);
    j::HSV YellowColor= j::HSV(0.1f, 1.f, 0.5f);
    j::HSV OffColor =   j::HSV(0.f, 0.f , 0.f);
};