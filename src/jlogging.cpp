// Copyright 2024 | Jake Rose 
//
// This file is part of project necklace_code_c
// See readme.md for full license details.

#include "jlogging.h"

#include <type_traits>

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

void jlog::print(const char *InMsg, Verbosity InVerbosity, Category InHideCategories)
{
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

void jlog::print(std::string InMsg, Verbosity InVerbosity, Category InHideCategories)
{
    jlog::print(InMsg.c_str(), InVerbosity, InHideCategories);
}

void jlog::print(String InMsg, Verbosity InVerbosity, Category InHideCategories)
{
    jlog::print(InMsg.c_str(), InVerbosity, InHideCategories);
}