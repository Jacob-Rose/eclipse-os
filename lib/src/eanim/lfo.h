// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <string>

#include <ecore/property.h>

#include <ecore/core.h>
#include <ecore/tickable.h>

#include <external/easing.h>

#include "attribute_float.h"

#include "generator_float.h"

using namespace ecore;

namespace eanim
{

    /* @brief A sine wave over position and time.
    *
    * evaluate(x) is the wave at `x`, scrolling by `speed` per second. `width`
    * is the wavelength, `amplitude` and `yOffset` scale and lift the output,
    * `xOffset` shifts the phase. Optionally shaped by an easing function.
    */
   class LFO : public Generator1D, public Tickable, public ecore::Reflectable
   {
   public:
       LFO();
       LFO(float inSpeed, float inWidth);

       // Tickable interface
       virtual void tick(float deltaTime) override;

       // Generator1D interface
       virtual float evaluate(float val) const override;

       float getCurrentOffset() const { return currentOffset; }

       // Reflectable interface: the phase, as `offset`
       virtual void reflect(ecore::PropertyBag& bag) override;

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
