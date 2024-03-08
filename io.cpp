// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "io.h"

void Button::init(uint8_t inPin)
{
    pinMode(inPin, INPUT_PULLUP);
    myPin = inPin;
}