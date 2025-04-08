// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include "../ecore/core.h"
#include "../ecore/tickable.h"

#include "../external/easing.h"

#include "attribute_float.h"

#include "generator_float.h"

using namespace ecore;

namespace eanim
{

    /* @brief Spring will help round out values and give things that bounciness that UX designers love
    *
    */
   class LFO : public Generator1D, public Tickable
   {
   public:
       LFO();
       LFO(float inSpeed, float inWidth);

       // Tickable interface
       virtual void tick(float deltaTime) override;

       // Generator1D interface
       virtual float evaluate(float val) const override;

       float getCurrentOffset() const { return currentOffset; }

       float speed = 1.0f;
       float width = 1.0f;
       float amplitude = 1.0f;
       float yOffset = 0.0f;
       float xOffset = 0.0f; // offsets on evaluate

       bool bUseEasingFunction{false};
       easing_functions easingFunction = easing_functions::EaseInOutElastic; // default to linear

       bool bShouldReflect{false};
   private:
       float currentOffset = 0.0f;
   };
} // namespace eanim
 