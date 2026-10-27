// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <ecore/core.h>

namespace eanim
{
    /* @brief Anything that can be read as one float right now.
    *
    * The common face of the float processors, the 1D generators and the
    * automation curve triggers, so a generator can take "a brightness" and be
    * handed a constant, a spring or a curve without caring which.
    *
    * @see FloatProcessor
    * @see Generator1D
    * @see AutomationCurveTrigger
    */
    class FloatAttribute
    {
    public:
        virtual ~FloatAttribute() = default;
        virtual float getValue() const = 0;
    };
}
