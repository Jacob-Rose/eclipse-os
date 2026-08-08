// Copyright 2024 | Jake Rose
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "fp.h"

using namespace efp;

// getFloat lives in fp.h as an inline. It used to be defined here at global
// scope, which meant it never matched the efp::getFloat the header declared.
