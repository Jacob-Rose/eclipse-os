#pragma once

class State
{
public:
    virtual void init();
    virtual void loop();
    virtual void checkInput();
};

class State_Boot : public State
{

};

class State_Emote : public State
{

};