// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../lib/esm/state.h"
#include "../lib/esm/state_generic.h"
#include "../lib/eio/relic.h"

#include "pendant.h"

using namespace ecore;
using namespace eio;

using namespace pendant;

namespace jacket
{
    class JacketCore : public PendantCore
    {
    public:
        JacketCore();

        virtual void init() override;
        virtual void tick(float deltaTime) override;

        void tick2();
        
    protected:
        std::unique_ptr<StateMachine_GenericHSV> stateMachine{ nullptr };
        std::unique_ptr<StateManager> stateManager{ nullptr };
        
        std::shared_ptr<State_GenericHSV> mainPatternState;
    };
}

