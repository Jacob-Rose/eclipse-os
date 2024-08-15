// Copyright 2024 | Jake Rose 
//
// This file is part of project eclipse-os
// See readme.md for full license details.

#include "logging.h"

#include <type_traits>

using namespace ecore;

Category operator&(Category lhs, Category rhs) {
    return static_cast<Category>(
        static_cast<std::underlying_type_t<Category>>(lhs) &
        static_cast<std::underlying_type_t<Category>>(rhs)
    );
}

Category operator|(Category lhs, Category rhs) {
    return static_cast<Category>(
        static_cast<std::underlying_type_t<Category>>(lhs) |
        static_cast<std::underlying_type_t<Category>>(rhs)
    );
}

void ecore::dbgLog(const char *InMsg, Verbosity InVerbosity, Category InHideCategories)
{
    //TODO FixMe
    /*
    if(ProjectVerbosity > InVerbosity)
    {
        return;
    }


    if((ProjectHideCategories & InHideCategories) != Category::None)
    {
        // easier for people to provide hide categories
        return;
    }
    */

    Serial.println(InMsg);
}