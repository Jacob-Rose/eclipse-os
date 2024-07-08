// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "jgenerator.h"

using namespace j;

void j::Generator1DToHSV::applyEffectLogic(std::vector<HSV> &InOutColors)
{
    if(generator.get() == nullptr)
    {
        return;
    }

    for(int i = 0; i < InOutColors.size(); ++i)
    {
        InOutColors[i] = palette.getColor(generator->evaluate(i));
    }
}


/*
CompositeGeneratorHSV::CompositeGeneratorHSV(uint16_t inLength) : GeneratorHSV(inLength)
{
}


PatternHSV::PatternHSV(uint16_t inLength) : length(inLength)
{
}
*/