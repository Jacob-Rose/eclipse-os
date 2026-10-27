// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include <esm/state.h>
#include <esm/state_generic.h>
#include <eio/relic.h>

#include "../pendant/pendant.h"

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
        
    protected:
        std::unique_ptr<StateMachine_GenericHSV> stateMachine{ nullptr };
        std::unique_ptr<StateManager> stateManager{ nullptr };
    };
}

