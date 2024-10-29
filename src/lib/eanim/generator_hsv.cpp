// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "generator_hsv.h"

using namespace eanim;



void GeneratorHSV_Cont::applyEffectLogic(uint16_t idx, HSV &InOutColor) const
{
    applyEffectLogic((float)idx, InOutColor);
}


void GeneratorHSV_Cont2D::applyEffectLogic(float x, HSV &InOutColor) const
{
    applyEffectLogic(x, 0.0f, InOutColor);
}
