// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>
#include "states/state.h"

#include "lib/eio/button.h"

using namespace ecore;
using namespace eio;

class Necklace
{
public:
    void setup();
    void setup1();

    void loop();
    void loop1();

    void tick();
    void tickScreen();

    void setActiveState(std::shared_ptr<State> NewState);


protected:
    // used for accurately simulating time between frames
    std::chrono::duration<double> lastFrameDT;
    std::chrono::time_point<std::chrono::system_clock> tickStartTime;

    // used for accurately simulating time between frames
    std::chrono::duration<double> lastFrameDT_Screen;
    std::chrono::time_point<std::chrono::system_clock> tickStartTime_Screen;

private:

    static bool runButtonHeldTestAndReset(Button* inButton);

    std::vector<std::shared_ptr<State>> States;
    std::shared_ptr<State> ActiveState;

    bool bSetupComplete = false;
};