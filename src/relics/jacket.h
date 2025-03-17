// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#pragma once

#include "../lib/esm/state.h"

#include "../lib/eio/relic.h"

using namespace ecore;
using namespace eio;

class JacketIO : public RelicIO
{
public:
    JacketIO();
};

class JacketCore : public RelicCore
{
public:
    JacketCore();
};