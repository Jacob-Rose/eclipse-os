// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "jgenerator.h"

using namespace j;

GeneratorHSV::GeneratorHSV(uint16_t inLength) : length(inLength)
{
}


CompositeGeneratorHSV::CompositeGeneratorHSV(uint16_t inLength) : GeneratorHSV(inLength)
{
}


PatternHSV::PatternHSV(uint16_t inLength) : length(inLength)
{
}