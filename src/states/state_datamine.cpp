#include "state_datamine.h"

#include "../gm.h"


State_Datamine::State_Datamine(const char* InStateName) : State(InStateName)
{

}

void State_Datamine::tickScreen()
{
    State::tickScreen();
}

void State_Datamine::tickLEDs()
{
    State::tickLEDs();

    GlobalManager& GM = GlobalManager::get();

    for(int idx = 0; idx < GM.RingLEDs->getLength(); ++idx)
    {
        GM.RingLEDs->setHSV(idx, j::HSV(0.5f, 1.0f, 1.0f));
    }
}

void State_Datamine::tickLogic()
{
    State::tickLogic();
}

void State_Datamine::onStateBegin()
{
    State::onStateBegin();
}