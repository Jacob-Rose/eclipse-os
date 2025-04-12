// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include "../../lib/esm/state.h"
#include "../../kits/effects.h"
#include "../../lib/eanim/lfo.h"
#include "../../lib/eanim/noise.h"

#include "../../lib/eanim/generator_hsv.h"

#include "../../lib/ecore/hsv.h"

#include "../../kits/palettes.h"

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
    

    

}

