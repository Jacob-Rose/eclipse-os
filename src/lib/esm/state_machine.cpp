#include "state_machine.h"

using namespace esm;

void StateMachine::setActiveState(shared_ptr<State> InNextState)
{
    NextState = InNextState;
    currentTransitionTime = 0.0f;
}

void StateMachine::init()
{
}

void StateMachine::cleanup()
{
}

void StateMachine::tick(float deltaTime)
{
    //TODO
}
