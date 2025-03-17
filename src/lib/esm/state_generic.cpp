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

void State_GenericHSV::onStateChangeState(StateStatus inStatus)
{
    State::onStateChangeState(inStatus);

    if(inStatus == StateStatus::TransitionIn)
    {
        // TODO Make this work
        bRenderToBuffer = true;
    }
}

void State_GenericHSV::tick(float deltaTime)
{
    State::tick(deltaTime);

    if(generator)
    {
        generator->tick(deltaTime);

        for(const auto& seg : io->strip_segments)
        {
            for(const std::shared_ptr<HSVStripNode>& node : seg.second->getNodes())
            {
                HSVStripNode* nodePtr = node.get();
                HSV color;
                generator->render(nodePtr, color);
                nodePtr->setHSV(color); // set the color on the node
            }
        }
    }
}