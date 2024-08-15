// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "generator_hsv.h"

using namespace eanim;


/*
CompositeGeneratorHSV::CompositeGeneratorHSV(uint16_t inLength) : GeneratorHSV(inLength)
{
}


PatternHSV::PatternHSV(uint16_t inLength) : length(inLength)
{
}
*/

void GeneratorHSV::applyEffectLogic(std::vector<HSV> &InOutColors) const
{
    for(int idx = 0; idx < InOutColors.size(); ++idx)
    {
        applyEffectLogic(idx, InOutColors[idx]);
    }
}

void GeneratorHSV::applyEffectLogic(uint16_t idx, HSV &InOutColor) const
{
    // expected to be overriden
    // not a pure virtual to support legacy applyEffectLogic(std::vector<HSV> &InOutColors)
}
