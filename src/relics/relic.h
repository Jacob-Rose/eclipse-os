// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>
#include <chrono>
#include <vector>
#include <map>

#include "../lib/eio/hsv_strip.h"

#include "../states/state.h"

using namespace ecore;
using namespace eio;
using namespace std;

enum class EBrightness
{
    NIGHTTRIP,
    MIN,
    MED,
    HIGH,
    BLINDING,
    MAX,
    COUNT 
};

uint8_t getEBrightnessAsByte(EBrightness inBrightness);

class RelicIO
{
public:

    RelicIO();

    void init();
    void cleanup();

    void tick(float deltaTime);
    void showLeds();

    
    EBrightness getGlobalBrightness() const;
    void setGlobalBrightness(EBrightness newBrightness);

private:
    // each relic can define an enum for the bytes to be per-device specific
    map<byte, shared_ptr<HSVStripSegment>> strip_segments;

    EBrightness currentBrightness;
}

class RelicDevice
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
private

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
}