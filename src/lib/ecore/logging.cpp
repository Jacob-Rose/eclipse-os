// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "logging.h"

#include "core.h"

#include <type_traits>

using namespace ecore::log;
using namespace std;


void ecore::log::dbgLog(const char *InMsg, Verbosity InVerbosity, Category InHideCategories)
{
    
    if(ProjectVerbosity > InVerbosity)
    {
        return;
    }


    if((ProjectHideCategories & InHideCategories) != Category::None)
    {
        // easier for people to provide hide categories
        return;
    }
    Serial.println(InMsg);
}