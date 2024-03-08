// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "necklace.h"

#include "logging.h"

#include "FastLED.h"

#include <chrono>
#include <ctime>

void Necklace::setup()
{
    std::shared_ptr<State> BootState = std::make_shared<State_Boot>("BootState");
    BootState->init();
    States.push_back(BootState);

    std::shared_ptr<State> EmoteState = std::make_shared<State_Emote>("EmoteState");
    EmoteState->init();
    States.push_back(EmoteState);

    std::shared_ptr<State> HeartbeatState = std::make_shared<State_Heartbeat>("HeartbeatState");
    HeartbeatState->init();
    States.push_back(HeartbeatState);

    ActiveState = HeartbeatState;

#if LOGGING_ENABLED
    std::string StartingStringLog = "Starting State: ";
    StartingStringLog.append(ActiveState->GetStateName());
    jlog::print(StartingStringLog.c_str(), Verbosity::Display);
#endif
}

void Necklace::tickLEDs()
{
    //std::time_t start_time = std::chrono::system_clock::now();
    ActiveState->tickLEDs();
    //std::time_t end_time = std::chrono::system_clock::now();
    //std::chrono::duration<float> tick_time = end_time - start_time;
    FastLED.show();
}

void Necklace::tickScreen()
{
    //std::time_t start_time = std::chrono::system_clock::now();
    ActiveState->tickScreen();
    //std::time_t end_time = std::chrono::system_clock::now();
    //std::chrono::duration<float> tick_time = end_time - start_time;
}

void Necklace::loop()
{
    tickLEDs();
    tickScreen(); // todo move to loop 1
    delay(20);
}

void Necklace::loop1()
{

}