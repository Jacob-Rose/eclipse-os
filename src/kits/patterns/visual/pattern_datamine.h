// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

// a reinterpretation of https://github.com/davepl/DavesGarageLEDSeries/blob/master/ fire effect
#pragma once

#include "../../../lib/eanim/lfo.h"
#include "../../../lib/eanim/generator_hsv.h"
#include "../../../lib/eanim/processor_float.h"

#include "../../palettes.h"

using namespace ecore;
using namespace eanim;

enum class EDatamineInputState
{
    Idle,
    Uploading,
    Downloading
};

// A relatively simple theater effect using a sine wave on the LED idx (+ an offset) to make data move.

// possibly a second sine wave that moves faster with a white pulse to visualize data transfer
class Pattern_Datamine : public GeneratorHSV
{
public:

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* node, HSV& inOutColor) const override;

    LFO lfoNecklaceInner = LFO(300.0f, 2.0f);
    LFO lfoNecklaceOuter = LFO(1600.0f, 8.0f);
    LFO lfoArm = LFO(-300.0f, 8.5f);
    LFO lfoWhip = LFO(1500.0f, 16.0f);

    EDatamineInputState currentState = EDatamineInputState::Idle;

    float idleSpeed = 20.0f;
    float uploadSpeed = 180.0f;
    float downloadSpeed = -180.0f;
    Momentum activationSpeedRamp = Momentum(50000.0f, 10000.0f);

    HSVPalette idlePalette = {
        HSV(180.f, 1.f, 1.f),
        HSV(270.f, 1.f, 1.f)
    };

    HSVPalette uploadPalette = {
        HSV(0.f, 1.f, 1.f),
        HSV(45.f, 1.f, 1.f)
    };

    HSVPalette downloadPalette = {
        HSV(0.3f, 1.f, 1.f),
        HSV(0.4f, 0.5f, 1.f)
    };

    private:
        float idlePaletteBuffer = 400.0f;
};