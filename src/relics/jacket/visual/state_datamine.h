// Copyright 2025 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../../../lib/eanim/lfo.h"
#include "../../../lib/eanim/generator_hsv.h"
#include "../../../lib/eanim/processor_float.h"

#include "../../pendant/pendant_generic_state.h"

#include "../../../kits/palettes.h"

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
    void init();

    virtual void tick(float deltaTime) override;
    virtual void render(HSVStripNode* node, HSV& inOutColor) const override;

    LFO lfoNecklace;
    LFO lfoArm;

    EDatamineInputState currentState = EDatamineInputState::Idle;

    float idleSpeed = 4.0f;
    float uploadSpeed = 24.0f;
    float downloadSpeed = -18.0f;
    Lerper activationSpeedRamp;

    HSVPalette idlePalette = {
        HSV(180.f, 1.f, 1.f),
        HSV(270.f, 1.f, 1.f)
    };

    HSVPalette uploadPalette = {
        HSV(0.f, 1.f, 1.f),
        HSV(45.f, 1.f, 1.f)
    };

    HSVPalette downloadPalette = {
        HSV(108.0f, 1.f, 1.f),
        HSV(144.0f, 0.5f, 1.f)
    };

    private:
        float currentActivationAmount = 0.0f;
        EDatamineInputState lastInputState;
        float idlePaletteBuffer = 400.0f;
};

class State_Datamine : public State_PendantGeneric
{
public:
    State_Datamine(const char* InStateName, RelicIO* inIO) : State_PendantGeneric(InStateName, inIO) {}

    virtual void init() override;
    virtual void tick(float deltaTime) override;
};