// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include <Arduino.h>

#include "../ecore/tickable.h"
#include "attribute_float.h"

using namespace ecore;

namespace eanim
{
    /* @brief Generator1D is the base interface for patterns that generate a float from a 1D coordinate (like an LED strip)
    * meant to be treated like a process, made to take in a positional input and generate a continous pattern from that float.
    *
    * For example, the provided positional value in many cases will be the led index on the strip.
    * 
    * @see LFO
    * @see Saw
    */
    class Generator1D : public FloatAttribute
    {
    public:
        virtual float evaluate(float val) const = 0;

        // we can assume that these work as float attributes in most cases
        virtual float getValue() const override { return evaluate(0.0f); }
    };


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
        float initialOffset = 0.0f;
    private:
        float currentOffset = 0.0f;
    };


    /* @brief Similar to an LFO, but linearly grows between 0-1, then clamps and resets back to zero 
    * the time it takes to reach 1 from 0 or vice versa = speed / fullWidth (fullWidth = width + gapWidth)
    * 
    * supports a gap between saw waves as well
    *
    */
    class Saw : public Generator1D, public Tickable
    {
    public:
        Saw();
        Saw(float inSpeed);

        // Tickable interface
        virtual void tick(float deltaTime) override;

        // Generator1D interface
        virtual float evaluate(float val) const override;

        float getCurrentOffset() const { return currentOffset; }

        float speed = 1.0f;
        float width = 1.0f;
        float initialOffset = 0.0f;
        float gapWidth = 0.0f;
    private:
        float currentOffset = 0.0f;
    };


    /* @brief Generator2D is the base interface for patterns that generate a float from a 2D coordinate
    * meant to be treated like a process, made to take in a positional input and generate a continous pattern from that float.
    * due to that nature, these processes should do there best to make this generative to save on memory usage
    * 
    * TODO Noise 2D generator
    * TODO Generator2D slice : pu
    */
    /*
    class Generator2D
    {
        virtual float evaluate(float value) const;
    };
    */

    /* @brief Generator2DLine allows us to query a single line (a Generator1D) from a Generator2D
    */
    /*
    class Generator2DLine : public Generator1D
    {
        virtual float evaluate(float value) const;
    };
    */
}