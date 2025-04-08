// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

// a reinterpretation of https://github.com/davepl/DavesGarageLEDSeries/blob/master/ fire effect
#pragma once

#include "../../../lib/eanim/lfo.h"
#include "../../../lib/eanim/generator_hsv.h"

#include "../../palettes.h"

using namespace ecore;
using namespace eanim;

class Pattern_BlueMagic : public GeneratorHSV
{
public:

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* node, HSV& inOutColor) const override;
    

    //j::FireEmitter fireEmitter;

    HSVPalette firePalette = jpalettes::p_bluemagic;

    LFO fireOffset = LFO(1.0f, 40.0f);

    LFO lfoNecklaceOuter = LFO(2.0f, 8.0f);
    LFO lfoInchwormSpeed = LFO(2.0f, 1.0f);
    float inchwormSpeed = 12.0f;
};