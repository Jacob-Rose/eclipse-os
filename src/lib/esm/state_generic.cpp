// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.
#include "state_generic.h"

#include <algorithm>

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

        for(const auto& seg : io->strip_segments)
        {
            for(const std::shared_ptr<HSVStripNode>& node : seg.second->getNodes())
            {
#if ERROR_CHECKING_ENABLED
                if(!node)
                {
                    std::string str = "State_GenericHSV::tick - node is null";
                    dbgLog(str.c_str(), Verbosity::Error, Category::Library);
                    continue;
                }
                if(!node->getStripSegment())
                {
                    std::string str = "State_GenericHSV::tick - node->getStripSegment() is null";
                    dbgLog(str.c_str(), Verbosity::Error, Category::Library);
                    continue;
                }
                if(!node->getStripSegment()->getParentStrip())
                {
                    std::string str = "State_GenericHSV::tick - node->getStripSegment()->getParentStrip() is null";
                    dbgLog(str.c_str(), Verbosity::Error, Category::Library);
                    continue;
                }
#endif
                HSV color = node->getStripSegment()->getParentStrip()->getHSV(node->getStripIdx());

                // A node the look leaves to what is underneath it is the
                // underlay's to paint, not the look's - see
                // GeneratorHSV::leavesToUnderlay. Anything else renders.
                const eanim::Underlay* underlay = generator->getUnderlay();
                if(!(underlay && generator->leavesToUnderlay(node.get()) && underlay->sample(node.get(), color)))
                {
                    generator->render(node.get(), color);
                }
                if(GetStatus() == StateStatus::TransitionIn || GetStatus() == StateStatus::TransitionOut)
                {
                    node->setBuffer(getStateID(), color);
                }
                else
                if(GetStatus() == StateStatus::Active)
                {
                    node->setHSV(color);
                }
            }
        }
    }
}

void StateMachine_GenericHSV::captureBlendSource()
{
    if(!io)
    {
        return;
    }
    for(const auto& seg : io->strip_segments)
    {
        HSVStrip* strip = seg.second->getParentStrip();
        for(const std::shared_ptr<HSVStripNode>& node : seg.second->getNodes())
        {
            node->setBuffer(kSnapshotBuffer, strip->getHSV(node->getStripIdx()));
        }
    }
}

void StateMachine_GenericHSV::tick(float deltaTime)
{
    StateMachine::tick(deltaTime);

    if(!io || !getNextState())
    {
        return;
    }

    // Both looks have rendered into their buffers; the blend decides what
    // each node shows between them. Where the outgoing picture comes from is
    // the machine's business (the active look, or a frozen mix after a
    // retarget) - see getBlendSourceBuffer.
    const float alpha = getTransitionProgress();
    const int fromBuffer = getBlendSourceBuffer();
    const int toBuffer = NextState->getStateID();
    const StateBlend& blend = getBlend();

    for(const auto& seg : io->strip_segments)
    {
        const std::vector<std::shared_ptr<HSVStripNode>>& nodes = seg.second->getNodes();
        const float span = nodes.size() > 1 ? static_cast<float>(nodes.size() - 1) : 1.0f;
        float position = 0.0f;

        for(const std::shared_ptr<HSVStripNode>& node : nodes)
        {
            const HSV from = fromBuffer == kNoBuffer ? HSV(0.f, 0.f, 0.f) : node->getBuffer(fromBuffer);
            const HSV to = node->getBuffer(toBuffer);
            node->setHSV(blend.mix(from, to, alpha, position / span));
            position += 1.0f;
        }
    }
}
