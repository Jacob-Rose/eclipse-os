// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>
#include "states.h"

class Necklace
{
public:
    void setup();
    void setup1();

    void loop();
    void loop1();

    void tickLEDs();
    void tickScreen();

    void setActiveState(std::shared_ptr<State> NewState);

private:

    std::vector<std::shared_ptr<State>> States;
    std::shared_ptr<State> ActiveState;
};