// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>
#include "../lib/esm/state.h"

using namespace ecore;
using namespace eio;

/*
class MappedHSVStrip_Obelisk : public MappedHSVStrip
{
    uint8_t WallLength = 42;
}
*/

class Obelisk
{
public:
    void setup();
    void setup1();

    void loop();
    void loop1();

    void tick();
    void tickScreen();

    float transitionTime = 8.0f;
    float timeForStates = 20.0f;

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
    std::shared_ptr<State> NextState;

    float currentTransitionTime;

    bool bSetupComplete = false;
};