// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "state_base.h"

#include <AnimatedGIF.h>


// Boot State also handles initializing the other states using the screen thread, 
// so code will look a little weird and hyperspecific
class State_Boot : public State
{
public:
    State_Boot(const char* InStateName);

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

    void addStateToInit(std::weak_ptr<State> stateToInit);
    bool hasInitializedAllStates() const { return bInitializedAllStates; }

    uint16_t currentHue = 0;

    float rotationSpeed = 0.5f;

private:
    float currentRotation; // normalized 0-1

    std::vector<std::weak_ptr<State>> statesToInit;
    bool bInitializedAllStates = false;
};

//psuedo-off for when charging
// todo set up, make fill screen and leds with black once then idle with long delays
class State_Off : public State
{
public:
    State_Off(const char* InStateName);

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;
};

class State_Emote : public State
{
public:
    State_Emote(const char* InStateName);

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;
};


class State_Heartbeat : public State
{
public:
    State_Heartbeat(const char* InStateName);

    virtual void init() override;

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

    AnimatedGIF HeartGif;

    float AnimTime{ 0.8f };

private:
    bool bLoadedHeart{false};

    float pulseAlpha = 0.0f;
};


class State_Hacker : public State
{
public:
    State_Hacker(const char* InStateName);

    virtual void init() override;

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

    // config 
    float screenRotateSpeed = 1.0f;

protected:


private:
    // state info
    float currentScreenAngle = 0;
};




class State_Serendipidy : public State
{
public:
    State_Serendipidy(const char* InStateName);

    virtual void init() override;

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

    AnimatedGIF img;

private:
};


class State_Settings : public State
{
public:
    State_Settings(const char* InStateName);

    virtual void init() override;

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

private:
};