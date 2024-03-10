// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "io.h"

#include <map>
#include <functional>
#include <memory>
#include <string>
#include <chrono>
#include <ctime>

#include <AnimatedGIF.h>

class GameState
{
public:
    static GameState* singleton;
    static void initSingleton();
    static void cleanupSingleton();
    static GameState* get() { return singleton; }

    void init();
    void cleanup();

    //TODO | 

    // delta time of last screen frame
    float LogicLastDT;
    float LEDLastDT;
    float ScreenLastDT; 

    // time we finished last logic frame
    float LogicLastTickTime; 
    float LEDLastTickTime;
    float ScreenLastTickTime;
};

class State
{
public:
    State(const char* InStateName);

    // init on start, pure init for setting up the state
    virtual void init();
    virtual void cleanup();

    virtual void stateActivated();
    virtual void stateDeactivated();

    // all deltaTimes are relative to their own hardware
    virtual void tickScreen();
    virtual void tickLEDs();
    // screen is rendered on seperate thread, thus we have two delta times
    virtual void tickLogic();

    // lambda passes in the owning state
    //void addStateTransition(std::weak_ptr<State> State, std::function<bool(State*)> Lambda);
    //void removeStateTransition(std::weak_ptr<State> State);

    const std::string& GetStateName() const { return stateName; }

    std::chrono::duration<float> GetStateActiveDuration() const;

private:
    //std::map<std::weak_ptr<State>, std::function<bool(State*)>> StateTransitions;

    std::chrono::time_point<std::chrono::system_clock> activationTime;

    std::string stateName;
};

class State_Boot : public State
{
public:
    State_Boot(const char* InStateName);

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;

    uint8_t currentLED = 0;
    uint8_t currentHue = 0;
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
};


class State_Hacker : public State
{
public:
    State_Hacker(const char* InStateName);

    virtual void init() override;

    virtual void tickScreen() override;
    virtual void tickLEDs() override;
    virtual void tickLogic() override;
};