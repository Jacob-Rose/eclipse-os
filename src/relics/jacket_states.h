// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include "../lib/esm/state.h"
#include "../lib/eanim/effects.h"
#include "../lib/ecore/hsv.h"
#include "../lib/eanim/generator_hsv.h"
#include "../kits/palettes.h"

using namespace ecore;
using namespace eanim;


namespace jacket
{

    class Pattern_Jacket_TheaterLFO : public GeneratorHSV 
    {
    public:
        Pattern_Jacket_TheaterLFO();
    
    public:
        LFO lfo;
        LFO paletteLFO;
        float speed = 1.0f;
    
        std::vector<ecore::HSVPalette> palette;
    
    public:
        virtual void tick(float deltaTime) override;
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };
    
    // Hold Button -> Charge Hand position
    // Release button -> release from hand and send down wire and arm (works for both held and attached)
    class Pattern_Jacket_ChargeHandPulse : public GeneratorHSV
    {
    public:
        Pattern_Jacket_ChargeHandPulse();
    
        virtual void tick(float deltaTime) override;
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    protected:
        float chargeRate = 1.0f;
        float maxCharge = 2.0f;
        float chargeToValueScalar = 10.0f;
    };
    
    
    // Hold Button A -> Upload Data
    // Hold Button B -> Download Data
    class Pattern_Jacket_MonowireDataTransfer : public GeneratorHSV
    {
    public:
        Pattern_Jacket_MonowireDataTransfer();
    
        virtual void tick(float deltaTime) override;
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };
    
    // Hold Button A -> Speed up turbines
    class Pattern_Jacket_WarpTurbines : public GeneratorHSV
    {
    public:
        Pattern_Jacket_WarpTurbines();
    
    public:
        HSVPalette turbinePaletteA { HSV(35.f, 0.75f, 0.9f), HSV(35.f, 0.05f, 0.5f) };
        HSVPalette turbinePaletteB { HSV(190.f, 0.75f, 0.9f), HSV(190.f, 0.05f, 0.5f)};
    
        LFO turbineLFO;
    
    public:
        virtual void init();
    
        virtual void tick(float deltaTime) override;
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    };
    
    class Pattern_Jacket_RainbowRoad : public GeneratorHSV
    {
    public:
        Pattern_Jacket_RainbowRoad();
    
    public:
        HSVPalette rainbowPalette { HSV(0.f, 0.55f, 0.7f), HSV(270.f, 0.55f, 0.7f) };

        LFO cogLFO;
        LFO cogSpeedLFO;

        virtual void init();
    
        virtual void tick(float deltaTime) override;
        virtual void render(HSVStripNode* inNode, HSV& inOutColor) const override;
    
    };
    
    
}

