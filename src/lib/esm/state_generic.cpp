// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_generic.h"

#include "../ecore/core.h"
#include "../ecore/math.h"
#include "../ecore/logging.h"

using namespace eanim;
using namespace esm;
using namespace ecore::log;

void State_GenericHSV::onStateChangeState(StateStatus inStatus)
{
    State::onStateChangeState(inStatus);
}

void State_GenericHSV::tick(float deltaTime)
{
    State::tick(deltaTime);

    if(generator)
    {
        generator->tick(deltaTime);

        dbgLog("ticking leds", Verbosity::Display);
        dbgLog("seg count: " + std::to_string(io->strip_segments.size()), Verbosity::Display);

        for(const auto& seg : io->strip_segments)
        {
            for(const std::shared_ptr<HSVStripNode>& node : seg.second->getNodes())
            {
                HSVStripNode* nodePtr = node.get();
                HSV color;
                generator->render(nodePtr, color);
                if(GetStatus() == StateStatus::TransitionIn || GetStatus() == StateStatus::TransitionOut)
                {
                    nodePtr->setBuffer(getStateID(), color);
                }
                
                if(GetStatus() == StateStatus::Active)
                {
                    nodePtr->setHSV(color);
                }
            }
        }
    }
}

void StateMachine_GenericHSV::tick(float deltaTime)
{
    StateMachine::tick(deltaTime);

    if(!io)
    {
        return;
    }

    if(getNextState())
    {
        //dbgLog("should be transitioning now");
        for(const auto& seg : io->strip_segments)
        {
            for(const std::shared_ptr<HSVStripNode>& node : seg.second->getNodes())
            {
                float alpha = currentTransitionTime / transitionTime;
                alpha = clamp(alpha, 0.0f, 1.0f); // ensure alpha is between 0 and 1

                HSV color = getActiveState() == nullptr ? HSV(0.f, 0.f, 0.f) : node->getBuffer(ActiveState->getStateID());
                HSV color2 = getNextState() == nullptr ? HSV(0.f, 0.f, 0.f) : node->getBuffer(NextState->getStateID());
                color.blendWith(color2, alpha); // blend the current color with the buffer color
                node->setHSV(color); // set the blended color to the node
            }
        }
    }
}
