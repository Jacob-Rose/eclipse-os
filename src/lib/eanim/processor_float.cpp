// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "processor_float.h"

#include <algorithm>

using namespace eanim;
using namespace ecore;

Momentum::Momentum()
{

}

Momentum::Momentum(float inAcceleration, float inMaxSpeed) 
    : acceleration(inAcceleration)
    , maxSpeed(inMaxSpeed)
{

}

void Momentum::tick(float deltaTime)
{
    float lastTickPosition = currentValue;

    float accelerationThisFrame = acceleration;
    if(targetValue < currentValue)
    {
        accelerationThisFrame *= -1.0f;
    }
    //currentSpeed += accelerationThisFrame * deltaTime; // this works for spring, will reuse
    currentSpeed = accelerationThisFrame;
    currentSpeed = std::clamp(currentSpeed, -maxSpeed, maxSpeed);
    currentValue += currentSpeed * deltaTime;
}

Spring::Spring(float inStrengthForce, float inDampening) : strengthForce(inStrengthForce), dampening(inDampening)
{

}

void Spring::tick(float deltaTime)
{
    float deltaPos = targetValue - currentValue;
    float force = deltaPos * strengthForce * dampening;
    currentAcceleration += force * deltaTime;
    currentValue += currentAcceleration * deltaTime;
}

std::shared_ptr<FloatAttribute> AttributeBuilder::makeLiteral(float value)
{
    std::shared_ptr<LiteralFloat> literalFloatAtt = std::shared_ptr<LiteralFloat>();
    literalFloatAtt->setTargetValue(value);
    return literalFloatAtt;
}