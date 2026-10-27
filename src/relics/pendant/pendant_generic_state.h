// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <memory>

#include <ecore/core.h>
#include <ecore/logging.h>
#include <ecore/delegate.h>

#include <eio/relic.h>
#include <eio/screen_drawer.h>

#include <esm/state_generic.h>

using namespace ecore;
using namespace eio;

class State_PendantGeneric : public State_GenericHSV
{
public:
    State_PendantGeneric(const char* inStateName, RelicIO* inIO) : State_GenericHSV(inStateName, inIO) {}

    virtual void onStateChangeState(StateStatus inStatus) override;

    virtual void tick(float deltaTime) override;

    void setStateStartGifData(uint8_t* inGifData, int inGifDataSize);

private:
    uint8_t* gifData{nullptr};
    int gifDataSize;

    bool bHasSetGif = false;



    std::shared_ptr<GeneratorHSV> generator;

};