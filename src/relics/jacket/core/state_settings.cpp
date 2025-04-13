// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.
#include "state_settings.h"

#include "../jacket_io.h"

#include "../../../imgs/gears.h"

using namespace jacket;


using namespace ecore;
using namespace ecore::log;
using namespace eio;


void Pattern_Settings::init()
{
    lfoGear.amplitude = 1.0f;
    lfoGear.yOffset = 0.5f;
    lfoGear.width = 0.00833f;
}

void Pattern_Settings::tick(float deltaTime)
{
    lfoGear.tick(deltaTime);
}

void Pattern_Settings::render(HSVStripNode *inNode, HSV &inOutColor) const
{
    if(!inNode)
    {
        dbgLog("Pattern_Jacket_WarpTurbines::render() - inNode is null!", Verbosity::Error);
        return;
    }

    if(inNode->GetStripNodeType() != StripNodeType::MAPPED2D)
    {
        dbgLog("Pattern_Jacket_WarpTurbines::render() - inNode is not a Mapped2D node!", Verbosity::Error);
        return;
    }
    HSVStripNode_Mapped2D *castedNode = static_cast<HSVStripNode_Mapped2D*>(inNode);

    float gearLFOEval = getEasingFunction(easing_functions::EaseInOutExpo)(lfoGear.evaluate(castedNode->coord.x));
    inOutColor = HSV(0.0f, 0.0f, gearLFOEval);
}



State_Settings::State_Settings(const char* InStateName, RelicIO* inIO) : State_PendantGeneric(InStateName, inIO)
{

}

void State_Settings::init()
{
    State_PendantGeneric::init();

    setGenerator(std::make_shared<Pattern_Settings>());
    setStateStartGifData((uint8_t *)gears, sizeof(gears));
}

void State_Settings::tick(float deltaTime)
{
    State_PendantGeneric::tick(deltaTime);

    JacketIO* jacketIO = static_cast<JacketIO*>(io);

    if(jacketIO->getBlueButton()->isPressed())
    {
        if(!bBlueButtonSeenPressed)
        {
            bBlueButtonSeenPressed = true;
            EBrightness currentBrightness = jacketIO->getGlobalBrightness();
            currentBrightness = (EBrightness)(((int)currentBrightness + 1) % (int)EBrightness::COUNT);
            jacketIO->setGlobalBrightness(currentBrightness);
        }
    }
    else
    {
        bBlueButtonSeenPressed = false;
    }
}
